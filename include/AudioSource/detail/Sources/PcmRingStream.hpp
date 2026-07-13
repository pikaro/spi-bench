#pragma once

#include "AudioSource/Interfaces/SourceConfig.hpp"
#include "AudioSource/Interfaces/Types.hpp"
#include "AudioSource/detail/PlatformSelect.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Totem::AudioSource::detail {

class NullAudioStream : public Platform::AudioStream {
  public:
    int available() override { return 0; }
    int availableForWrite() override { return 0; }
    void flush() override {}
    size_t write(const uint8_t *, size_t) override { return 0; }
    size_t readBytes(uint8_t *, size_t) override { return 0; }
    operator bool() override { return false; }
};

class PcmRingStream : public Platform::AudioStream {
  public:
    DELETE_COPY(PcmRingStream)
    DELETE_MOVE(PcmRingStream)

    PcmRingStream() = default;

    ReturnCode begin(AudioInfo audioInfo) {
        _audioInfo = audioInfo;
        Platform::setAudioInfo(*this, _audioInfo);
        clear();
        _active.store(true, std::memory_order_release);
        return OK();
    }

    void end() {
        _active.store(false, std::memory_order_release);
        clear();
    }

    [[nodiscard]] std::size_t capacity() const { return _buffer.size(); }

    [[nodiscard]] std::size_t bytesAvailable() {
        return _usedSnapshot.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint32_t droppedBytes() const {
        return _droppedBytes.load(std::memory_order_acquire);
    }

    void clear() {
        TryGuard guard{_lock};
        if (!guard) {
            _clearRequested.store(true, std::memory_order_release);
            return;
        }
        _clearLocked();
    }

    size_t writePcm(const uint8_t *data, size_t len) {
        if (!_active.load(std::memory_order_acquire) || data == nullptr ||
            len == 0) {
            return 0;
        }

        TryGuard guard{_lock};
        if (!guard) {
            _droppedBytes.fetch_add(static_cast<uint32_t>(len),
                                    std::memory_order_acq_rel);
            return 0;
        }
        _handlePendingClearLocked();
        auto incoming = len;
        if (incoming >= _buffer.size()) {
            _droppedBytes.fetch_add(
                static_cast<uint32_t>(_used + incoming - _buffer.size()),
                std::memory_order_acq_rel);
            data += incoming - _buffer.size();
            incoming = _buffer.size();
            _read = 0;
            _write = 0;
            _used = 0;
        } else {
            const auto free = _buffer.size() - _used;
            if (incoming > free) {
                const auto drop = incoming - free;
                _read = (_read + drop) % _buffer.size();
                _used -= drop;
                _droppedBytes.fetch_add(static_cast<uint32_t>(drop),
                                        std::memory_order_acq_rel);
            }
        }

        const auto first = std::min(incoming, _buffer.size() - _write);
        std::copy_n(data, first, _buffer.data() + _write);
        std::copy_n(data + first, incoming - first, _buffer.data());
        _write = (_write + incoming) % _buffer.size();
        _used += incoming;
        _publishUsedLocked();
        return len;
    }

    size_t writeStereo16AsMono(const uint8_t *data, size_t len) {
        if (!_active.load(std::memory_order_acquire) || data == nullptr ||
            len < 4) {
            return 0;
        }

        TryGuard guard{_lock};
        const auto frames = len / 4U;
        if (!guard) {
            _droppedBytes.fetch_add(static_cast<uint32_t>(frames * 2U),
                                    std::memory_order_acq_rel);
            return 0;
        }
        _handlePendingClearLocked();
        auto framesToWrite = frames;
        std::size_t firstFrame = 0;
        auto outgoing = framesToWrite * 2U;
        if (outgoing >= _buffer.size()) {
            framesToWrite = _buffer.size() / 2U;
            firstFrame = frames - framesToWrite;
            outgoing = framesToWrite * 2U;
            _droppedBytes.fetch_add(
                static_cast<uint32_t>(_used + (frames * 2U) - outgoing),
                std::memory_order_acq_rel);
            _read = 0;
            _write = 0;
            _used = 0;
        } else {
            const auto free = _buffer.size() - _used;
            if (outgoing > free) {
                const auto drop = outgoing - free;
                _read = (_read + drop) % _buffer.size();
                _used -= drop;
                _droppedBytes.fetch_add(static_cast<uint32_t>(drop),
                                        std::memory_order_acq_rel);
            }
        }
        for (std::size_t frame = firstFrame; frame < frames; ++frame) {
            const auto *frameData = data + (frame * 4U);
            const auto left = static_cast<int16_t>(
                static_cast<uint16_t>(frameData[0]) |
                (static_cast<uint16_t>(frameData[1]) << 8U));
            const auto right = static_cast<int16_t>(
                static_cast<uint16_t>(frameData[2]) |
                (static_cast<uint16_t>(frameData[3]) << 8U));
            const auto mono = static_cast<int16_t>(
                (static_cast<int32_t>(left) + static_cast<int32_t>(right)) /
                2);
            _writeByteLocked(static_cast<uint8_t>(mono & 0xFF));
            _writeByteLocked(static_cast<uint8_t>(
                (static_cast<uint16_t>(mono) >> 8U) & 0xFFU));
        }
        _publishUsedLocked();
        return frames * 4U;
    }

    int available() override {
        if (!_active.load(std::memory_order_acquire)) {
            return 0;
        }
        return static_cast<int>(
            std::min<std::size_t>(bytesAvailable(),
                                  std::numeric_limits<int>::max()));
    }

    int availableForWrite() override {
        if (!_active.load(std::memory_order_acquire)) {
            return 0;
        }
        return static_cast<int>(std::min<std::size_t>(
            _buffer.size() - bytesAvailable(),
            std::numeric_limits<int>::max()));
    }

    void flush() override {}

    size_t write(const uint8_t *data, size_t len) override {
        return writePcm(data, len);
    }

    size_t readBytes(uint8_t *data, size_t len) override {
        if (!_active.load(std::memory_order_acquire) || data == nullptr ||
            len == 0) {
            return 0;
        }

        TryGuard guard{_lock};
        if (!guard) {
            return 0;
        }
        _handlePendingClearLocked();
        const auto count = std::min(len, _used);
        const auto first = std::min(count, _buffer.size() - _read);
        std::copy_n(_buffer.data() + _read, first, data);
        std::copy_n(_buffer.data(), count - first, data + first);
        _read = (_read + count) % _buffer.size();
        _used -= count;
        _publishUsedLocked();
        return count;
    }

    operator bool() override {
        return _active.load(std::memory_order_acquire);
    }

  private:
    struct TryGuard {
        explicit TryGuard(std::atomic_flag &flag)
            : lock(flag),
              locked(!lock.test_and_set(std::memory_order_acquire)) {}

        ~TryGuard() {
            if (locked) {
                lock.clear(std::memory_order_release);
            }
        }

        explicit operator bool() const { return locked; }

        std::atomic_flag &lock;
        bool locked;
    };

    [[nodiscard]] std::size_t _next(std::size_t index) const {
        ++index;
        return index == _buffer.size() ? 0 : index;
    }

    void _writeByteLocked(uint8_t value) {
        _buffer[_write] = value;
        _write = _next(_write);
        ++_used;
    }

    void _clearLocked() {
        _read = 0;
        _write = 0;
        _used = 0;
        _clearRequested.store(false, std::memory_order_release);
        _droppedBytes.store(0, std::memory_order_release);
        _publishUsedLocked();
    }

    void _handlePendingClearLocked() {
        if (_clearRequested.exchange(false, std::memory_order_acq_rel)) {
            _read = 0;
            _write = 0;
            _used = 0;
            _publishUsedLocked();
        }
    }

    void _publishUsedLocked() {
        _usedSnapshot.store(_used, std::memory_order_release);
    }

    AudioInfo _audioInfo{};
    std::array<uint8_t, a2dpSourceBufferBytes> _buffer{};
    std::atomic_flag _lock = ATOMIC_FLAG_INIT;
    std::atomic<bool> _active{false};
    std::atomic<bool> _clearRequested{false};
    std::atomic<uint32_t> _droppedBytes{0};
    std::atomic<std::size_t> _usedSnapshot{0};
    std::size_t _read = 0;
    std::size_t _write = 0;
    std::size_t _used = 0;
};

} // namespace Totem::AudioSource::detail
