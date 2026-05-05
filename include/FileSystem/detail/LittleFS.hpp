#pragma once

#include "Base/HasLifecycle.hpp"
#include "FileSystem/Interfaces/Config.hpp"
#include "FileSystem/Interfaces/Types.hpp"
#include "FileSystem/detail/FileChunkReader.hpp"
#include "FileSystem/detail/Path.hpp"
#include "FileSystem/detail/PlatformSelect.hpp"
#include "FileSystem/detail/Types.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <span>
#include <string_view>
#include <utility>

namespace Totem::FileSystem::detail {

class LittleFS : public HasLifecycle<LittleFS, Totem::FileSystem::Config> {
    friend class HasLifecycle<LittleFS, Totem::FileSystem::Config>;
    friend struct LifecycleContract<LittleFS, Totem::FileSystem::Config>;

  public:
    DELETE_COPY(LittleFS)
    DELETE_MOVE(LittleFS)

    using File = Platform::File;
    using Directory = Platform::Directory;
    using Config = Totem::FileSystem::Config;

    static constexpr const char *name = "FileSystem::LittleFS";
    static constexpr LogComponent logComponent =
        Totem::FileSystem::detail::logComponent;

    LittleFS() = default;

    [[nodiscard]] bool exists(std::string_view path) const {
        FAIL_IF_INACTIVE(false, "Cannot check file existence before mount");
        PathBuffer<Config::maxPathLength> fullPath{};
        auto full = makeFullPath(fullPath, config().basePath, path);
        if (!full) {
            return false;
        }
        auto stats = Platform::statPath(fullPath.c_str());
        if (!stats && stats.error() != ERR(CoreError, NotFound)) {
            _log_e("Failed to stat " SV_FMT ": " ERR_FMT, SV_ARG(path),
                   ERR_ARG(stats.error()));
        }
        return stats.has_value();
    }

    [[nodiscard]] bool isDir(std::string_view path) const {
        FAIL_IF_INACTIVE(false, "Cannot stat directory before mount");
        PathBuffer<Config::maxPathLength> fullPath{};
        auto full = makeFullPath(fullPath, config().basePath, path);
        if (!full) {
            return false;
        }
        auto stats = Platform::statPath(fullPath.c_str());
        if (!stats) {
            return false;
        }
        return stats->directory;
    }

    std::expected<std::size_t, ReturnCode>
    fileSize(std::string_view path) const {
        FAIL_IF_INACTIVE_UNEXPECTED("Cannot stat file before mount");
        auto stats = _statLogicalPath(path);
        if (!stats) {
            return std::unexpected(stats.error());
        }
        FAIL_IF(stats->directory,
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "Path is a directory: " SV_FMT, SV_ARG(path));
        return stats->size;
    }

    std::expected<std::size_t, ReturnCode>
    readFile(std::string_view path, std::span<std::byte> out) const {
        FAIL_IF_INACTIVE_UNEXPECTED("Cannot read file before mount");

        PathBuffer<Config::maxPathLength> fullPath{};
        auto full = makeFullPath(fullPath, config().basePath, path);
        if (!full) {
            return std::unexpected(full.error());
        }

        auto stats = Platform::statPath(fullPath.c_str());
        if (!stats) {
            return std::unexpected(stats.error());
        }
        FAIL_IF(stats->directory,
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "Cannot read directory as file: " SV_FMT, SV_ARG(path));
        FAIL_IF(stats->size > out.size(),
                std::unexpected(ERR(CoreError, InvalidSize)),
                "Read buffer too small for " SV_FMT ": %zu > %zu", SV_ARG(path),
                stats->size, out.size());

        File file{};
        auto openRet = file.openRead(fullPath.c_str());
        if (!openRet.ok()) {
            return std::unexpected(openRet);
        }

        auto read = file.read(out.first(stats->size));
        if (!read) {
            (void)file.close();
            return std::unexpected(read.error());
        }
        if (*read != stats->size) {
            (void)file.close();
            return std::unexpected(ERR(CoreError, InvalidSize));
        }

        auto closeRet = file.close();
        if (!closeRet.ok()) {
            return std::unexpected(closeRet);
        }
        return *read;
    }

    ReturnCode openRead(File &file, std::string_view path) const {
        FAIL_IF_INACTIVE_ERR("Cannot open file before mount");

        PathBuffer<Config::maxPathLength> fullPath{};
        auto full = makeFullPath(fullPath, config().basePath, path);
        if (!full) {
            return full.error();
        }

        auto stats = Platform::statPath(fullPath.c_str());
        if (!stats) {
            return stats.error();
        }
        FAIL_IF(stats->directory, ERR(CoreError, InvalidArgument),
                "Cannot open directory as file: " SV_FMT, SV_ARG(path));

        return file.openRead(fullPath.c_str());
    }

    ReturnCode openAppend(File &file, std::string_view path) const {
        FAIL_IF_INACTIVE_ERR("Cannot open file before mount");

        PathBuffer<Config::maxPathLength> fullPath{};
        auto full = makeFullPath(fullPath, config().basePath, path);
        if (!full) {
            return full.error();
        }

        auto stats = Platform::statPath(fullPath.c_str());
        if (stats) {
            FAIL_IF(stats->directory, ERR(CoreError, InvalidArgument),
                    "Cannot append to directory: " SV_FMT, SV_ARG(path));
        } else if (stats.error() != ERR(CoreError, NotFound)) {
            return stats.error();
        }

        return file.openAppend(fullPath.c_str());
    }

    ReturnCode openAppendQuiet(File &file, std::string_view path) const {
        if (!active()) {
            return ERR(CoreError, InvalidState);
        }

        PathBuffer<Config::maxPathLength> fullPath{};
        auto full = makeFullPath(fullPath, config().basePath, path);
        if (!full) {
            return full.error();
        }

        auto stats = Platform::statPath(fullPath.c_str());
        if (stats) {
            if (stats->directory) {
                return ERR(CoreError, InvalidArgument);
            }
        } else if (stats.error() != ERR(CoreError, NotFound)) {
            return stats.error();
        }

        return file.openAppendQuiet(fullPath.c_str());
    }

    ReturnCode appendFile(std::string_view path, std::string_view data) const {
        File file{};
        FAIL_IF_ERR_FWD(openAppend(file, path), "Failed to open " SV_FMT,
                        SV_ARG(path));
        auto written = file.write(data);
        if (!written) {
            (void)file.close();
            return written.error();
        }
        if (*written != data.size()) {
            (void)file.close();
            FAIL(ERR(CoreError, InvalidSize),
                 "Short write to " SV_FMT ": %zu != %zu", SV_ARG(path),
                 *written, data.size());
        }
        FAIL_IF_ERR_FWD(file.flush(), "Failed to flush " SV_FMT,
                        SV_ARG(path));
        return file.close();
    }

    template <std::size_t ChunkSize>
    ReturnCode openReader(BasicFileChunkReader<File, ChunkSize> &reader,
                          std::string_view path) const {
        FAIL_IF_INACTIVE_ERR("Cannot open file reader before mount");

        PathBuffer<Config::maxPathLength> fullPath{};
        auto full = makeFullPath(fullPath, config().basePath, path);
        if (!full) {
            return full.error();
        }
        auto stats = Platform::statPath(fullPath.c_str());
        if (!stats) {
            return stats.error();
        }
        FAIL_IF(stats->directory, ERR(CoreError, InvalidArgument),
                "Cannot open directory as file: " SV_FMT, SV_ARG(path));

        File file{};
        FAIL_IF_ERR_FWD(file.openRead(fullPath.c_str()),
                        "Failed to open " SV_FMT, SV_ARG(path));
        return reader.begin(std::move(file));
    }

    template <std::size_t ChunkSize>
    ReturnCode getFileReader(BasicFileChunkReader<File, ChunkSize> &reader,
                             std::string_view path) const {
        return openReader(reader, path);
    }

    template <typename Visitor>
        requires(std::is_invocable_r_v<ReturnCode, Visitor, const FileInfo &>)
    ReturnCode
    forEachFile(std::string_view path, Visitor &&visitor,
                std::span<const std::string_view> suffixes = {}) const {
        return _forEachEntry(path, std::forward<Visitor>(visitor), suffixes,
                             EntryMode::FilesOnly);
    }

    template <typename Visitor>
        requires(std::is_invocable_r_v<ReturnCode, Visitor, const FileInfo &>)
    ReturnCode forEachEntry(std::string_view path, Visitor &&visitor) const {
        return _forEachEntry(path, std::forward<Visitor>(visitor), {},
                             EntryMode::All);
    }

    std::expected<StorageInfo, ReturnCode> info() const {
        FAIL_IF_INACTIVE_UNEXPECTED("Cannot query LittleFS info before mount");
        return Platform::littleFSInfo(config());
    }

  private:
    enum class EntryMode : uint8_t {
        FilesOnly,
        All,
    };

    template <typename Visitor>
        requires(std::is_invocable_r_v<ReturnCode, Visitor, const FileInfo &>)
    ReturnCode _forEachEntry(std::string_view path, Visitor &&visitor,
                             std::span<const std::string_view> suffixes,
                             EntryMode entryMode) const {
        FAIL_IF_INACTIVE_ERR("Cannot list files before mount");

        PathBuffer<Config::maxPathLength> fullDirPath{};
        auto fullDir = makeFullPath(fullDirPath, config().basePath, path);
        if (!fullDir) {
            return fullDir.error();
        }

        Directory dir{};
        FAIL_IF_ERR_FWD(dir.open(fullDirPath.c_str()),
                        "Failed to open directory " SV_FMT, SV_ARG(path));

        for (;;) {
            auto nameResult = dir.nextName();
            if (!nameResult) {
                (void)dir.close();
                return nameResult.error();
            }
            const char *entryName = *nameResult;
            if (entryName == nullptr) {
                break;
            }
            if (_isDotEntry(entryName)) {
                continue;
            }

            const std::string_view nameView{entryName, std::strlen(entryName)};
            if (!_matchesSuffix(nameView, suffixes)) {
                continue;
            }

            PathBuffer<Config::maxPathLength> fullEntryPath{};
            auto fullEntry =
                makeChildPath(fullEntryPath, fullDirPath.view(), nameView);
            if (!fullEntry) {
                (void)dir.close();
                return fullEntry.error();
            }

            auto stats = Platform::statPath(fullEntryPath.c_str());
            if (!stats) {
                (void)dir.close();
                return stats.error();
            }
            if (entryMode == EntryMode::FilesOnly && stats->directory) {
                continue;
            }

            PathBuffer<Config::maxPathLength> logicalEntryPath{};
            auto logicalEntry = makeChildPath(
                logicalEntryPath, path.empty() ? std::string_view{"/"} : path,
                nameView);
            if (!logicalEntry) {
                (void)dir.close();
                return logicalEntry.error();
            }

            const FileInfo info{
                .path = logicalEntryPath.view(),
                .name = nameView,
                .size = stats->size,
                .directory = stats->directory,
            };
            auto ret = std::invoke(std::forward<Visitor>(visitor), info);
            if (!ret.ok()) {
                (void)dir.close();
                return ret;
            }
        }

        return dir.close();
    }

    ReturnCode _onBegin() {
        _log_i("Mounting LittleFS partition %s at %s", config().partitionLabel,
               config().basePath);
        FAIL_IF_ERR_FWD(Platform::mountLittleFS(config()),
                        "LittleFS mount failed");

        auto infoResult = Platform::littleFSInfo(config());
        if (infoResult) {
            _log_i("LittleFS mounted: used=%zu total=%zu",
                   infoResult->usedBytes, infoResult->totalBytes);
        }
        return OK();
    }

    ReturnCode _onEnd() { return Platform::unmountLittleFS(config()); }

    std::expected<Platform::FileStats, ReturnCode>
    _statLogicalPath(std::string_view path) const {
        PathBuffer<Config::maxPathLength> fullPath{};
        auto full = makeFullPath(fullPath, config().basePath, path);
        if (!full) {
            return std::unexpected(full.error());
        }
        return Platform::statPath(fullPath.c_str());
    }

    [[nodiscard]] static bool _isDotEntry(const char *name) {
        return name[0] == '.' &&
               (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
    }

    [[nodiscard]] static bool
    _matchesSuffix(std::string_view name,
                   std::span<const std::string_view> suffixes) {
        if (suffixes.empty()) {
            return true;
        }
        for (auto suffix : suffixes) {
            if (name.ends_with(suffix)) {
                return true;
            }
        }
        return false;
    }
};

} // namespace Totem::FileSystem::detail
