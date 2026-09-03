// IWYU pragma: private

#pragma once

#include "BatteryMonitor/Interfaces/Config.hpp"
#include "BatteryMonitor/Interfaces/Types.hpp"
#include "BatteryMonitor/detail/JournalFormat.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>

namespace Totem::BatteryMonitor::detail {

enum class JournalScanIssue : uint8_t {
    None,
    Corrupt,
    Oversized,
};

struct JournalScanResult {
    BatteryProfile latestProfile{};
    uint32_t highestSessionId = 0;
    uint32_t completeSessionCount = 0;
    uint32_t incompatibleSessionCount = 0;
    uint32_t incompleteSessionCount = 0;
    uint32_t corruptRecordCount = 0;
    uint32_t journalRecordCount = 0;
    uint32_t danglingSessionId = 0;
    uint32_t danglingNextSequence = 0;
    uint32_t danglingRecordsChecksum = 0;
    JournalScanIssue issue = JournalScanIssue::None;
};

[[nodiscard]] constexpr JournalScanResult
classifyJournalSize(uint64_t totalBytes, uint32_t maximumRecords) {
    JournalScanResult result{};
    const uint64_t completeRecords = totalBytes / journalRecordSize;
    if (completeRecords > maximumRecords) {
        result.issue = JournalScanIssue::Oversized;
        result.journalRecordCount = maximumRecords;
        return result;
    }
    result.journalRecordCount = static_cast<uint32_t>(completeRecords);
    if ((totalBytes % journalRecordSize) != 0) {
        result.issue = JournalScanIssue::Corrupt;
    }
    return result;
}

template <typename Reader>
concept JournalRecordReader = requires(Reader reader) {
    { reader.readNext() } -> std::same_as<std::expected<bool, ReturnCode>>;
    { reader.size() } -> std::same_as<std::size_t>;
    { reader.span() } -> std::same_as<std::span<const std::byte>>;
};

/** Scans a bounded stream of fixed journal records without owning storage. */
template <JournalRecordReader Reader>
[[nodiscard]] std::expected<JournalScanResult, ReturnCode>
scanJournalRecords(Reader &reader, const BatteryConfig &config,
                   uint32_t maximumRecords, JournalScanResult result) {
    struct Candidate {
        BatteryProfile profile{};
        std::array<bool, profilePointCount> seenPoints{};
        uint32_t sessionId = 0;
        uint32_t nextSequence = 0;
        uint32_t recordsChecksum = 0;
        uint16_t pointCount = 0;
        bool compatible = false;
        bool active = false;
    };

    const auto saturatingIncrement = [](uint32_t value) {
        return value == std::numeric_limits<uint32_t>::max() ? value
                                                             : value + 1U;
    };
    const uint32_t expectedConfigHash = configHash(config);
    Candidate candidate{};
    uint32_t recordsRead = 0;
    for (;;) {
        auto next = reader.readNext();
        if (!next) {
            return std::unexpected(next.error());
        }
        if (!*next) {
            break;
        }
        if (reader.size() != journalRecordSize) {
            result.issue = JournalScanIssue::Corrupt;
            break;
        }
        if (recordsRead >= maximumRecords) {
            result.issue = JournalScanIssue::Oversized;
            break;
        }
        ++recordsRead;

        JournalRecord raw{};
        std::copy(reader.span().begin(), reader.span().end(), raw.begin());
        DecodedJournalRecord record{};
        if (!decodeJournalRecord(raw, record)) {
            result.corruptRecordCount =
                saturatingIncrement(result.corruptRecordCount);
            result.issue = JournalScanIssue::Corrupt;
            candidate = {};
            continue;
        }
        result.highestSessionId =
            std::max(result.highestSessionId, record.sessionId);

        if (record.type == JournalRecordType::SessionHeader) {
            if (candidate.active) {
                result.incompleteSessionCount =
                    saturatingIncrement(result.incompleteSessionCount);
            }
            candidate = {
                .sessionId = record.sessionId,
                .nextSequence = record.sequence + 1U,
                .recordsChecksum = record.crc,
                .compatible = getU32(record.payload, 0) == expectedConfigHash,
                .active = true,
            };
            continue;
        }

        if (!candidate.active || record.sessionId != candidate.sessionId ||
            record.sequence != candidate.nextSequence) {
            result.issue = JournalScanIssue::Corrupt;
            result.corruptRecordCount =
                saturatingIncrement(result.corruptRecordCount);
            candidate = {};
            continue;
        }
        ++candidate.nextSequence;

        if (record.type == JournalRecordType::ProfilePoint) {
            const auto point = decodeProfilePointPayload(record.payload);
            if (point.stateOfChargePercent <= 100U) {
                const auto index =
                    static_cast<size_t>(point.stateOfChargePercent);
                if (!candidate.seenPoints[index]) {
                    candidate.seenPoints[index] = true;
                    ++candidate.pointCount;
                }
                if (candidate.compatible) {
                    candidate.profile.points[index] = {
                        .loadedVoltageMillivolts =
                            point.loadedVoltageMillivolts,
                        .representativeCurrentMicroamps =
                            point.representativeCurrentMicroamps,
                    };
                }
            }
        }

        if (record.type != JournalRecordType::SessionFooter) {
            candidate.recordsChecksum ^= record.crc;
            continue;
        }

        const auto footer = decodeFooterPayload(record.payload);
        const bool complete =
            footer.state == BatteryCalibrationState::Complete &&
            footer.reason == BatteryCalibrationInvalidReason::None &&
            footer.recordsChecksum == candidate.recordsChecksum &&
            footer.pointCount == profilePointCount &&
            candidate.pointCount == profilePointCount &&
            footer.usableMilliampHours > 0 && footer.usableMilliwattHours > 0;
        if (complete) {
            result.completeSessionCount =
                saturatingIncrement(result.completeSessionCount);
            if (candidate.compatible) {
                candidate.profile.status = {
                    .sessionId = candidate.sessionId,
                    .usableMilliampHours = footer.usableMilliampHours,
                    .usableMilliwattHours = footer.usableMilliwattHours,
                    .durationSeconds = footer.durationSeconds,
                    .observedCutoffMillivolts = footer.observedCutoffMillivolts,
                    .pointCount = footer.pointCount,
                    .active = true,
                };
                result.latestProfile = candidate.profile;
            } else {
                result.incompatibleSessionCount =
                    saturatingIncrement(result.incompatibleSessionCount);
            }
        } else {
            result.incompleteSessionCount =
                saturatingIncrement(result.incompleteSessionCount);
        }
        candidate = {};
    }

    if (candidate.active) {
        result.incompleteSessionCount =
            saturatingIncrement(result.incompleteSessionCount);
        if (candidate.compatible) {
            result.danglingSessionId = candidate.sessionId;
            result.danglingNextSequence = candidate.nextSequence;
            result.danglingRecordsChecksum = candidate.recordsChecksum;
        }
    }
    return result;
}

} // namespace Totem::BatteryMonitor::detail
