// IWYU pragma: private

#pragma once

#include "FileSystem/Interfaces/Config.hpp"
#include "FileSystem/Interfaces/Types.hpp"
#include "FileSystem/detail/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include "esp_littlefs.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <expected>
#include <span>
#include <string_view>
#include <sys/stat.h>
#include <utility>

namespace Totem::FileSystem::detail::platform {

struct FileStats {
    std::size_t size = 0;
    bool directory = false;
};

inline ReturnCode mapErrno(int error) {
    switch (error) {
    case 0:
        return OK();
    case ENOENT:
        return ERR(CoreError, NotFound);
    case EACCES:
    case EPERM:
        return ERR(CoreError, Forbidden);
    case ENOMEM:
    case ENOSPC:
        return ERR(CoreError, OutOfMemory);
    case ENAMETOOLONG:
        return ERR(CoreError, InvalidSize);
    case EINVAL:
    case ENOTDIR:
    case EISDIR:
        return ERR(CoreError, InvalidArgument);
    default:
        return ERR(CoreError, OperationFailed);
    }
}

inline ReturnCode mapErrnoOrFailure(int error) {
    return error == 0 ? ERR(CoreError, OperationFailed) : mapErrno(error);
}

class File {
  public:
    DELETE_COPY(File)

    File() = default;
    ~File() { (void)close(); }

    File(File &&other) noexcept { *this = std::move(other); }

    File &operator=(File &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        (void)close();
        _file = std::exchange(other._file, nullptr);
        return *this;
    }

    ReturnCode openRead(const char *path) {
        FAIL_IF_NULL(path, ERR(CoreError, InvalidArgument),
                     "Cannot open null filesystem path");
        FAIL_IF(_file != nullptr, ERR(CoreError, InvalidState),
                "File handle is already open");
        errno = 0;
        _file = std::fopen(path, "rb");
        FAIL_IF(_file == nullptr, mapErrnoOrFailure(errno), "Failed to open %s",
                path);
        return OK();
    }

    ReturnCode openAppend(const char *path) {
        FAIL_IF_NULL(path, ERR(CoreError, InvalidArgument),
                     "Cannot open null filesystem path");
        FAIL_IF(_file != nullptr, ERR(CoreError, InvalidState),
                "File handle is already open");
        return openAppendQuiet(path);
    }

    ReturnCode openAppendQuiet(const char *path) {
        if (path == nullptr) {
            return ERR(CoreError, InvalidArgument);
        }
        if (_file != nullptr) {
            return ERR(CoreError, InvalidState);
        }
        errno = 0;
        _file = std::fopen(path, "ab");
        if (_file == nullptr) {
            return mapErrnoOrFailure(errno);
        }
        return OK();
    }

    std::expected<std::size_t, ReturnCode> read(std::span<std::byte> out) {
        FAIL_IF(_file == nullptr, std::unexpected(ERR(CoreError, InvalidState)),
                "Cannot read from closed file");
        if (out.empty()) {
            return 0;
        }
        FAIL_IF(out.data() == nullptr,
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "Read buffer is null");

        errno = 0;
        const auto read =
            std::fread(out.data(), sizeof(std::byte), out.size(), _file);
        if (read < out.size() && std::ferror(_file) != 0) {
            return std::unexpected(mapErrnoOrFailure(errno));
        }
        return read;
    }

    std::expected<std::size_t, ReturnCode>
    write(std::span<const std::byte> data) {
        FAIL_IF(_file == nullptr, std::unexpected(ERR(CoreError, InvalidState)),
                "Cannot write to closed file");
        if (data.empty()) {
            return 0;
        }
        FAIL_IF(data.data() == nullptr,
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "Write buffer is null");

        errno = 0;
        const auto written =
            std::fwrite(data.data(), sizeof(std::byte), data.size(), _file);
        if (written < data.size()) {
            return std::unexpected(mapErrnoOrFailure(errno));
        }
        return written;
    }

    std::expected<std::size_t, ReturnCode> write(std::string_view data) {
        return write(std::as_bytes(std::span(data.data(), data.size())));
    }

    ReturnCode flush() {
        FAIL_IF(_file == nullptr, ERR(CoreError, InvalidState),
                "Cannot flush closed file");
        return flushQuiet();
    }

    ReturnCode flushQuiet() {
        if (_file == nullptr) {
            return ERR(CoreError, InvalidState);
        }
        errno = 0;
        const auto ret = std::fflush(_file);
        if (ret != 0) {
            return mapErrnoOrFailure(errno);
        }
        return OK();
    }

    ReturnCode close() {
        if (_file == nullptr) {
            return OK();
        }
        errno = 0;
        auto *file = std::exchange(_file, nullptr);
        const auto ret = std::fclose(file);
        FAIL_IF(ret != 0, mapErrnoOrFailure(errno), "Failed to close file");
        return OK();
    }

    ReturnCode closeQuiet() {
        if (_file == nullptr) {
            return OK();
        }
        errno = 0;
        const auto ret = std::fclose(std::exchange(_file, nullptr));
        if (ret != 0) {
            return mapErrnoOrFailure(errno);
        }
        return OK();
    }

    [[nodiscard]] bool open() const { return _file != nullptr; }

  private:
    std::FILE *_file = nullptr;
};

class Directory {
  public:
    DELETE_COPY(Directory)

    Directory() = default;
    ~Directory() { (void)close(); }

    Directory(Directory &&other) noexcept { *this = std::move(other); }

    Directory &operator=(Directory &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        (void)close();
        _dir = std::exchange(other._dir, nullptr);
        return *this;
    }

    ReturnCode open(const char *path) {
        FAIL_IF_NULL(path, ERR(CoreError, InvalidArgument),
                     "Cannot open null directory path");
        FAIL_IF(_dir != nullptr, ERR(CoreError, InvalidState),
                "Directory handle is already open");
        errno = 0;
        _dir = ::opendir(path);
        FAIL_IF(_dir == nullptr, mapErrnoOrFailure(errno),
                "Failed to open directory %s", path);
        return OK();
    }

    std::expected<const char *, ReturnCode> nextName() {
        FAIL_IF(_dir == nullptr, std::unexpected(ERR(CoreError, InvalidState)),
                "Cannot read from closed directory");
        errno = 0;
        auto *entry = ::readdir(_dir);
        if (entry == nullptr) {
            if (errno != 0) {
                return std::unexpected(mapErrno(errno));
            }
            return static_cast<const char *>(nullptr);
        }
        return entry->d_name;
    }

    ReturnCode close() {
        if (_dir == nullptr) {
            return OK();
        }
        errno = 0;
        const auto ret = ::closedir(std::exchange(_dir, nullptr));
        FAIL_IF(ret != 0, mapErrnoOrFailure(errno),
                "Failed to close directory");
        return OK();
    }

  private:
    DIR *_dir = nullptr;
};

inline std::expected<FileStats, ReturnCode> statPath(const char *path) {
    FAIL_IF_NULL(path, std::unexpected(ERR(CoreError, InvalidArgument)),
                 "Cannot stat null filesystem path");

    struct stat result{};
    errno = 0;
    if (::stat(path, &result) != 0) {
        return std::unexpected(mapErrnoOrFailure(errno));
    }

    const bool directory = S_ISDIR(result.st_mode);
    FAIL_IF(!directory && result.st_size < 0,
            std::unexpected(ERR(CoreError, InvalidData)),
            "Filesystem returned negative file size for %s", path);
    return FileStats{
        .size = directory ? 0 : static_cast<std::size_t>(result.st_size),
        .directory = directory,
    };
}

inline ReturnCode removePath(const char *path) {
    FAIL_IF_NULL(path, ERR(CoreError, InvalidArgument),
                 "Cannot remove null filesystem path");

    errno = 0;
    const auto ret = std::remove(path);
    FAIL_IF(ret != 0, mapErrnoOrFailure(errno), "Failed to remove %s", path);
    return OK();
}

inline ReturnCode mountLittleFS(const Config &config) {
    esp_vfs_littlefs_conf_t vfsConfig{};
    vfsConfig.base_path = config.basePath;
    vfsConfig.partition_label = config.partitionLabel;
    vfsConfig.partition = nullptr;
    vfsConfig.format_if_mount_failed = config.formatIfMountFailed;
    vfsConfig.read_only = false;
    vfsConfig.dont_mount = config.dontMount;
    vfsConfig.grow_on_mount = false;
    auto ret =
        ::platform::map_platform_error(esp_vfs_littlefs_register(&vfsConfig));
    FAIL_IF_ERR_FWD(ret, "Failed to mount LittleFS partition %s at %s",
                    config.partitionLabel, config.basePath);
    return OK();
}

inline ReturnCode unmountLittleFS(const Config &config) {
    auto ret = ::platform::map_platform_error(
        esp_vfs_littlefs_unregister(config.partitionLabel));
    FAIL_IF_ERR_FWD(ret, "Failed to unmount LittleFS partition %s",
                    config.partitionLabel);
    return OK();
}

inline std::expected<StorageInfo, ReturnCode>
littleFSInfo(const Config &config) {
    std::size_t totalBytes = 0;
    std::size_t usedBytes = 0;
    auto ret = ::platform::map_platform_error(
        esp_littlefs_info(config.partitionLabel, &totalBytes, &usedBytes));
    if (!ret.ok()) {
        return std::unexpected(ret);
    }
    return StorageInfo{
        .totalBytes = totalBytes,
        .usedBytes = usedBytes,
    };
}

struct Platform {
    using FileStats = Totem::FileSystem::detail::platform::FileStats;
    using File = Totem::FileSystem::detail::platform::File;
    using Directory = Totem::FileSystem::detail::platform::Directory;

    static std::expected<FileStats, ReturnCode> statPath(const char *path) {
        return Totem::FileSystem::detail::platform::statPath(path);
    }

    static ReturnCode removePath(const char *path) {
        return Totem::FileSystem::detail::platform::removePath(path);
    }

    static ReturnCode mountLittleFS(const Config &config) {
        return Totem::FileSystem::detail::platform::mountLittleFS(config);
    }

    static ReturnCode unmountLittleFS(const Config &config) {
        return Totem::FileSystem::detail::platform::unmountLittleFS(config);
    }

    static std::expected<StorageInfo, ReturnCode>
    littleFSInfo(const Config &config) {
        return Totem::FileSystem::detail::platform::littleFSInfo(config);
    }
};

} // namespace Totem::FileSystem::detail::platform
