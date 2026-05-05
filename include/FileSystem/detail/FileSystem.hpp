#pragma once

#include "FileSystem/detail/FileChunkReader.hpp"
#include <cstddef>
#include <span>
#include <string_view>
#include <utility>

namespace Totem::FileSystem::detail {

template <class Implementation> class FileSystem {
  public:
    using Config = typename Implementation::Config;
    using File = typename Implementation::File;

    template <std::size_t ChunkSize = Config::defaultChunkSize>
    using ChunkReader = BasicFileChunkReader<File, ChunkSize>;

    ReturnCode begin(const Config &config = {}) { return _impl.begin(config); }
    ReturnCode end() { return _impl.end(); }

    [[nodiscard]] bool active() const { return _impl.active(); }
    [[nodiscard]] bool exists(std::string_view path) const {
        return _impl.exists(path);
    }
    [[nodiscard]] bool isDir(std::string_view path) const {
        return _impl.isDir(path);
    }

    auto fileSize(std::string_view path) const { return _impl.fileSize(path); }

    auto readFile(std::string_view path, std::span<std::byte> out) const {
        return _impl.readFile(path, out);
    }

    ReturnCode openRead(File &file, std::string_view path) const {
        return _impl.openRead(file, path);
    }

    ReturnCode openAppend(File &file, std::string_view path) const {
        return _impl.openAppend(file, path);
    }

    ReturnCode openAppendQuiet(File &file, std::string_view path) const {
        return _impl.openAppendQuiet(file, path);
    }

    ReturnCode appendFile(std::string_view path, std::string_view data) const {
        return _impl.appendFile(path, data);
    }

    template <std::size_t ChunkSize>
    ReturnCode openReader(ChunkReader<ChunkSize> &reader,
                          std::string_view path) const {
        return _impl.openReader(reader, path);
    }

    template <std::size_t ChunkSize>
    ReturnCode getFileReader(ChunkReader<ChunkSize> &reader,
                             std::string_view path) const {
        return _impl.getFileReader(reader, path);
    }

    template <typename Visitor>
    ReturnCode
    forEachFile(std::string_view path, Visitor &&visitor,
                std::span<const std::string_view> suffixes = {}) const {
        return _impl.forEachFile(path, std::forward<Visitor>(visitor),
                                 suffixes);
    }

    template <typename Visitor>
    ReturnCode forEachEntry(std::string_view path, Visitor &&visitor) const {
        return _impl.forEachEntry(path, std::forward<Visitor>(visitor));
    }

    auto info() const { return _impl.info(); }

  private:
    Implementation _impl{};
};

} // namespace Totem::FileSystem::detail
