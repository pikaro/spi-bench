#pragma once

#include "AudioSource/Interfaces/Types.hpp"
#include "AudioTools/CoreAudio/BaseStream.h"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace AiAudio {

inline constexpr Totem::AudioSource::AudioInfo nemoAsrPcmAudio{
    .sampleRate = 16000,
    .channels = 1,
    .bitsPerSample = 16,
};

struct Pcm16DownsamplerConfig {
    Totem::AudioSource::AudioInfo input{
        .sampleRate = 32000,
        .channels = 1,
        .bitsPerSample = 32,
    };
    Totem::AudioSource::AudioInfo output = nemoAsrPcmAudio;
    uint8_t inputRightShift = 16;

    [[nodiscard]] constexpr bool validate() const {
        return input.sampleRate == output.sampleRate * 2U &&
               input.channels == 1 && output.channels == 1 &&
               input.bitsPerSample == 32 && output.bitsPerSample == 16 &&
               inputRightShift <= 31;
    }

    [[nodiscard]] constexpr bool
    accepts(Totem::AudioSource::AudioInfo actual) const {
        return validate() && actual.sampleRate == input.sampleRate &&
               actual.channels == input.channels &&
               actual.bitsPerSample == input.bitsPerSample;
    }
};

class Pcm16DownsamplerStream : public audio_tools::AudioStream {
  public:
    DELETE_COPY(Pcm16DownsamplerStream)
    DELETE_MOVE(Pcm16DownsamplerStream)

    Pcm16DownsamplerStream() = default;

    ReturnCode begin(audio_tools::AudioStream &source,
                     Totem::AudioSource::AudioInfo input,
                     Pcm16DownsamplerConfig config) {
        FAIL_IF(_sourceStream != nullptr, ERR(CoreError, InvalidState),
                "PCM16 downsampler is already active");
        FAIL_IF(!config.accepts(input), ERR(CoreError, InvalidArgument),
                "PCM16 downsampler requires 32000 Hz, 32-bit, mono input");

        _sourceStream = &source;
        _config = config;
        _resetBuffers();
        audio_tools::AudioStream::setAudioInfo(_toAudioTools(_config.output));
        return OK();
    }

    void end() override {
        _sourceStream = nullptr;
        _resetBuffers();
    }

    [[nodiscard]] bool active() const { return _sourceStream != nullptr; }

    size_t readBytes(uint8_t *data, size_t len) override {
        if (_sourceStream == nullptr || data == nullptr || len == 0) {
            return 0;
        }

        size_t written = 0;
        if (_pendingByteValid) {
            data[written++] = _pendingByte;
            _pendingByteValid = false;
            if (written == len) {
                return written;
            }
        }

        while (written + 1U < len) {
            int16_t sample = 0;
            if (!_readDownsampled(sample)) {
                return written;
            }
            _writeLe16(sample, data + written);
            written += sizeof(int16_t);
        }

        if (written < len) {
            int16_t sample = 0;
            if (_readDownsampled(sample)) {
                const auto pcm = static_cast<uint16_t>(sample);
                data[written++] = static_cast<uint8_t>(pcm & 0xFFU);
                _pendingByte = static_cast<uint8_t>((pcm >> 8U) & 0xFFU);
                _pendingByteValid = true;
            }
        }

        return written;
    }

    size_t write(const uint8_t *, size_t) override { return 0; }

    int available() override {
        if (_sourceStream == nullptr) {
            return 0;
        }
        return _sourceStream->available() / 4;
    }

    int availableForWrite() override { return 0; }

    void flush() override {}

    operator bool() override { return active(); }

  private:
    static constexpr std::size_t sourceSampleBytes = sizeof(int32_t);
    static constexpr std::size_t rawBufferBytes = 512;

    static audio_tools::AudioInfo
    _toAudioTools(Totem::AudioSource::AudioInfo info) {
        return audio_tools::AudioInfo{
            info.sampleRate,
            info.channels,
            info.bitsPerSample,
        };
    }

    void _resetBuffers() {
        _rawOffset = 0;
        _rawSize = 0;
        _heldSample = 0;
        _heldSampleValid = false;
        _pendingByte = 0;
        _pendingByteValid = false;
    }

    bool _readDownsampled(int16_t &out) {
        int32_t first = 0;
        if (_heldSampleValid) {
            first = _heldSample;
            _heldSampleValid = false;
        } else if (!_readSourceSample(first)) {
            return false;
        }

        int32_t second = 0;
        if (!_readSourceSample(second)) {
            _heldSample = first;
            _heldSampleValid = true;
            return false;
        }

        const auto averaged =
            (static_cast<int64_t>(first) + static_cast<int64_t>(second)) / 2;
        out = _toPcm16(averaged);
        return true;
    }

    bool _readSourceSample(int32_t &out) {
        if (!_ensureRawBytes(sourceSampleBytes)) {
            return false;
        }

        const auto *ptr = _raw.data() + _rawOffset;
        const uint32_t raw =
            static_cast<uint32_t>(ptr[0]) |
            (static_cast<uint32_t>(ptr[1]) << 8U) |
            (static_cast<uint32_t>(ptr[2]) << 16U) |
            (static_cast<uint32_t>(ptr[3]) << 24U);
        out = static_cast<int32_t>(raw);
        _rawOffset += sourceSampleBytes;
        if (_rawOffset == _rawSize) {
            _rawOffset = 0;
            _rawSize = 0;
        }
        return true;
    }

    bool _ensureRawBytes(std::size_t required) {
        while (_rawSize - _rawOffset < required) {
            _compactRawBuffer();
            if (!_readMoreRaw()) {
                return false;
            }
        }
        return true;
    }

    void _compactRawBuffer() {
        if (_rawOffset == 0) {
            return;
        }
        const auto remaining = _rawSize - _rawOffset;
        if (remaining > 0) {
            std::memmove(_raw.data(), _raw.data() + _rawOffset, remaining);
        }
        _rawOffset = 0;
        _rawSize = remaining;
    }

    bool _readMoreRaw() {
        if (_sourceStream == nullptr) {
            return false;
        }

        const auto freeBytes = _raw.size() - _rawSize;
        if (freeBytes == 0) {
            return false;
        }

        const auto bytesRead =
            _sourceStream->readBytes(_raw.data() + _rawSize, freeBytes);
        if (bytesRead == 0) {
            return false;
        }

        _rawSize += bytesRead;
        return true;
    }

    int16_t _toPcm16(int64_t sample) const {
        const auto shifted = sample >> _config.inputRightShift;
        const auto clamped = std::clamp<int64_t>(
            shifted, std::numeric_limits<int16_t>::min(),
            std::numeric_limits<int16_t>::max());
        return static_cast<int16_t>(clamped);
    }

    static void _writeLe16(int16_t sample, uint8_t *out) {
        const auto pcm = static_cast<uint16_t>(sample);
        out[0] = static_cast<uint8_t>(pcm & 0xFFU);
        out[1] = static_cast<uint8_t>((pcm >> 8U) & 0xFFU);
    }

    audio_tools::AudioStream *_sourceStream = nullptr;
    Pcm16DownsamplerConfig _config{};
    std::array<uint8_t, rawBufferBytes> _raw{};
    std::size_t _rawOffset = 0;
    std::size_t _rawSize = 0;
    int32_t _heldSample = 0;
    bool _heldSampleValid = false;
    uint8_t _pendingByte = 0;
    bool _pendingByteValid = false;
};

inline constexpr Pcm16DownsamplerConfig defaultNemoAsrDownsamplerConfig{};
static_assert(defaultNemoAsrDownsamplerConfig.validate());

} // namespace AiAudio
