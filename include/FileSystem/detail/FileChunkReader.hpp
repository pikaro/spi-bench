#pragma once

#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <expected>
#include <span>
#include <utility>

namespace Totem::FileSystem::detail {

template <class File, std::size_t ChunkSize> class BasicFileChunkReader {
    static_assert(ChunkSize > 0, "ChunkSize must be greater than zero");

  public:
    DELETE_COPY(BasicFileChunkReader)

    BasicFileChunkReader() = default;
    ~BasicFileChunkReader() { (void)close(); }

    BasicFileChunkReader(BasicFileChunkReader &&other) noexcept {
        *this = std::move(other);
    }

    BasicFileChunkReader &operator=(BasicFileChunkReader &&other) noexcept {
        if (this == &other) {
            return *this;
        }

        (void)close();
        _file = std::move(other._file);
        _buffer = other._buffer;
        _size = other._size;
        _totalSize = other._totalSize;
        _chunks = other._chunks;
        _active = other._active;

        other._size = 0;
        other._totalSize = 0;
        other._chunks = 0;
        other._active = false;
        return *this;
    }

    ReturnCode begin(File &&file) {
        (void)close();
        FAIL_IF(!file.open(), ERR(CoreError, InvalidArgument),
                "Cannot begin FileChunkReader with a closed file");
        _file = std::move(file);
        _size = 0;
        _totalSize = 0;
        _chunks = 0;
        _active = true;
        return OK();
    }

    std::expected<bool, ReturnCode> readNext() {
        FAIL_IF(!_active, std::unexpected(ERR(CoreError, InvalidState)),
                "FileChunkReader is not active");

        auto readResult = _file.read(std::span<std::byte>{_buffer});
        if (!readResult) {
            _active = false;
            (void)_file.close();
            return std::unexpected(readResult.error());
        }

        _size = *readResult;
        if (_size == 0) {
            _active = false;
            auto closeRet = _file.close();
            if (!closeRet.ok()) {
                return std::unexpected(closeRet);
            }
            return false;
        }

        FAIL_IF(static_cast<std::size_t>(-1) - _totalSize < _size,
                std::unexpected(ERR(CoreError, Overflow)),
                "FileChunkReader total size overflow");
        ++_chunks;
        _totalSize += _size;
        return true;
    }

    [[nodiscard]] bool next() {
        auto result = readNext();
        return result.has_value() && *result;
    }

    ReturnCode close() {
        _active = false;
        _size = 0;
        return _file.close();
    }

    [[nodiscard]] std::span<const std::byte> span() const {
        return std::span<const std::byte>{_buffer.data(), _size};
    }

    [[nodiscard]] const std::byte *data() const { return _buffer.data(); }
    [[nodiscard]] std::size_t size() const { return _size; }
    [[nodiscard]] std::size_t totalSize() const { return _totalSize; }
    [[nodiscard]] std::size_t chunks() const { return _chunks; }
    [[nodiscard]] bool active() const { return _active; }

  private:
    File _file{};
    std::array<std::byte, ChunkSize> _buffer{};
    std::size_t _size = 0;
    std::size_t _totalSize = 0;
    std::size_t _chunks = 0;
    bool _active = false;
};

} // namespace Totem::FileSystem::detail
