// IWYU pragma: private

#pragma once

#include "BatteryMonitor/Interfaces/Config.hpp"
#include "BatteryMonitor/Interfaces/Types.hpp"
#include "BatteryMonitor/detail/JournalFormat.hpp"
#include "BatteryMonitor/detail/JournalScanner.hpp"
#include "BatteryMonitor/detail/JournalStorage.hpp"
#include "FileSystem/Facade.hpp"
#include "Services/FileSystem.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string_view>

namespace Totem::BatteryMonitor::detail {

struct CurveBuildResult {
    std::array<BatteryProfilePoint, profilePointCount> points{};
    uint32_t intervalCount = 0;
};

struct CurveBuilder {
    using Reader =
        FileSystemService::DefaultFileSystem::ChunkReader<journalRecordSize>;

    Reader reader{};
    CurveBuildResult result{};
    std::array<uint64_t, profilePointCount> bestDistance{};
    uint32_t sessionId = 0;
    uint32_t totalMilliampHours = 0;
    uint32_t processedRecords = 0;
    bool active = false;
};

/**
 * LittleFS adapter for the append-only calibration journal.
 *
 * The logical path is copied during begin(), so no caller-owned string is
 * retained. Every append is flushed and closed before success is reported;
 * after reset, preceding complete CRC-protected records remain readable and a
 * partial trailing record is detected and never appended through.
 */
class Journal {
  public:
    [[nodiscard]] ReturnCode begin(const BatteryConfig &config) {
        const std::string_view sourcePath{config.profilePath};
        if (sourcePath.empty() || sourcePath.size() >= _path.size()) {
            return ERR(CoreError, InvalidArgument);
        }
        std::copy(sourcePath.begin(), sourcePath.end(), _path.begin());
        _path[sourcePath.size()] = '\0';
        _maximumRecords = config.maximumJournalRecords;
        _initialized = true;
        return OK();
    }

    void end() {
        _path.fill('\0');
        _maximumRecords = 0;
        _initialized = false;
    }

    [[nodiscard]] ReturnCode append(const JournalRecord &record) const {
        if (!FileSystemService::configured() || !_initialized) {
            return ERR(CoreError, InvalidState);
        }

        auto capacity = canFitRecords(1);
        if (!capacity) {
            return capacity.error();
        }
        if (!*capacity) {
            return ERR(CoreError, Overflow);
        }

        FileSystemAppendSink sink{FileSystemService::get(), path()};
        return appendJournalRecord(sink, record);
    }

    [[nodiscard]] std::expected<bool, ReturnCode>
    canFitSession(const BatteryConfig &config) const {
        return canFitRecords(config.maximumSessionRecords());
    }

    [[nodiscard]] std::expected<JournalScanResult, ReturnCode>
    scan(const BatteryConfig &config) const {
        JournalScanResult result{};
        if (!FileSystemService::configured() || !_initialized) {
            return std::unexpected(ERR(CoreError, InvalidState));
        }
        auto &fileSystem = FileSystemService::get();
        auto size = fileSystem.fileSize(path());
        if (!size) {
            if (size.error() == ReturnCode::from(CoreError::NotFound)) {
                return result;
            }
            return std::unexpected(size.error());
        }
        result = classifyJournalSize(*size, _maximumRecords);
        if (result.issue == JournalScanIssue::Oversized) {
            return result;
        }

        CurveBuilder::Reader reader{};
        auto openRet = fileSystem.openReader(reader, path());
        if (!openRet.ok()) {
            return std::unexpected(openRet);
        }

        return scanJournalRecords(reader, config, _maximumRecords, result);
    }

    [[nodiscard]] ReturnCode beginCurveBuild(uint32_t sessionId,
                                             uint32_t totalMilliampHours,
                                             CurveBuilder &builder) const {
        if (!FileSystemService::configured() || !_initialized ||
            totalMilliampHours == 0 || builder.active) {
            return ERR(CoreError, InvalidState);
        }

        auto size = FileSystemService::get().fileSize(path());
        if (!size) {
            return size.error();
        }
        if ((*size % journalRecordSize) != 0 ||
            (*size / journalRecordSize) > _maximumRecords) {
            return ERR(CoreError, InvalidData);
        }

        CurveBuilder next{};
        next.bestDistance.fill(std::numeric_limits<uint64_t>::max());
        next.sessionId = sessionId;
        next.totalMilliampHours = totalMilliampHours;
        auto openRet = FileSystemService::get().openReader(next.reader, path());
        if (!openRet.ok()) {
            return openRet;
        }
        next.active = true;
        builder = std::move(next);
        return OK();
    }

    /** Processes no more than `recordBudget` fixed-size journal records. */
    [[nodiscard]] std::expected<bool, ReturnCode>
    workCurveBuild(CurveBuilder &builder, uint32_t recordBudget) const {
        if (!builder.active || recordBudget == 0) {
            return std::unexpected(ERR(CoreError, InvalidState));
        }

        for (uint32_t step = 0; step < recordBudget; ++step) {
            auto next = builder.reader.readNext();
            if (!next) {
                builder.active = false;
                return std::unexpected(next.error());
            }
            if (!*next) {
                builder.active = false;
                if (builder.result.intervalCount == 0) {
                    return std::unexpected(ERR(CoreError, InvalidData));
                }
                return true;
            }
            if (builder.reader.size() != journalRecordSize ||
                builder.processedRecords >= _maximumRecords) {
                (void)builder.reader.close();
                builder.active = false;
                return std::unexpected(ERR(CoreError, InvalidData));
            }
            ++builder.processedRecords;

            JournalRecord raw{};
            std::copy(builder.reader.span().begin(),
                      builder.reader.span().end(), raw.begin());
            DecodedJournalRecord record{};
            if (!decodeJournalRecord(raw, record)) {
                (void)builder.reader.close();
                builder.active = false;
                return std::unexpected(ERR(CoreError, InvalidData));
            }
            if (record.sessionId != builder.sessionId ||
                record.type != JournalRecordType::Interval) {
                continue;
            }

            const auto interval = decodeIntervalPayload(record.payload);
            builder.result.intervalCount =
                saturatingIncrement(builder.result.intervalCount);
            for (uint32_t socPercent = 0; socPercent <= 100; ++socPercent) {
                const uint32_t dischargedPercent = 100U - socPercent;
                const uint64_t measured =
                    static_cast<uint64_t>(interval.cumulativeMilliampHours) *
                    100U;
                const uint64_t target =
                    static_cast<uint64_t>(builder.totalMilliampHours) *
                    dischargedPercent;
                const uint64_t distance =
                    measured > target ? measured - target : target - measured;
                if (distance >= builder.bestDistance[socPercent]) {
                    continue;
                }
                builder.bestDistance[socPercent] = distance;
                builder.result.points[socPercent] = {
                    .loadedVoltageMillivolts =
                        interval.averageVoltageMillivolts,
                    .representativeCurrentMicroamps =
                        interval.averageCurrentMicroamps,
                };
            }
        }
        return false;
    }

  private:
    class FileSystemAppendSink {
      public:
        FileSystemAppendSink(FileSystemService::DefaultFileSystem &fileSystem,
                             std::string_view path)
            : _fileSystem{fileSystem}, _path{path} {}

        ReturnCode open() { return _fileSystem.openAppendQuiet(_file, _path); }

        std::expected<std::size_t, ReturnCode>
        write(std::span<const std::byte> bytes) {
            return _file.write(bytes);
        }

        ReturnCode flush() { return _file.flushQuiet(); }
        ReturnCode close() { return _file.closeQuiet(); }

      private:
        FileSystemService::DefaultFileSystem &_fileSystem;
        std::string_view _path;
        FileSystemService::DefaultFileSystem::File _file{};
    };

    [[nodiscard]] std::expected<bool, ReturnCode>
    canFitRecords(uint32_t additionalRecords) const {
        if (!FileSystemService::configured() || !_initialized) {
            return std::unexpected(ERR(CoreError, InvalidState));
        }
        auto &fileSystem = FileSystemService::get();
        size_t currentBytes = 0;
        auto size = fileSystem.fileSize(path());
        if (size) {
            currentBytes = *size;
            if ((currentBytes % journalRecordSize) != 0) {
                return std::unexpected(ERR(CoreError, InvalidData));
            }
        } else if (size.error() != ReturnCode::from(CoreError::NotFound)) {
            return std::unexpected(size.error());
        }

        const uint64_t currentRecords = currentBytes / journalRecordSize;
        auto storage = fileSystem.info();
        if (!storage) {
            return std::unexpected(storage.error());
        }
        const size_t freeBytes = storage->usedBytes >= storage->totalBytes
                                     ? 0
                                     : storage->totalBytes - storage->usedBytes;
        return journalRecordsFit(currentRecords, additionalRecords,
                                 _maximumRecords, freeBytes);
    }

    [[nodiscard]] std::string_view path() const {
        return std::string_view{_path.data()};
    }

    [[nodiscard]] static constexpr uint32_t
    saturatingIncrement(uint32_t value) {
        return value == std::numeric_limits<uint32_t>::max() ? value
                                                             : value + 1U;
    }

    std::array<char, FileSystemConfig::maxPathLength> _path{};
    uint32_t _maximumRecords = 0;
    bool _initialized = false;
};

} // namespace Totem::BatteryMonitor::detail
