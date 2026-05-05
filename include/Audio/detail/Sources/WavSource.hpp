#pragma once

#include "Audio/Interfaces/SourceConfig.hpp"
#include "Audio/Interfaces/Types.hpp"
#include "Audio/detail/PlatformSelect.hpp"
#include "Audio/detail/Sources/IAudioSource.hpp"
#include "Audio/detail/Types.hpp"
#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "Services/FileSystem.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace Totem::Audio::detail {

class WavSource : public HasLifecycle<WavSource, WavSourceConfig>,
                  public IAudioSource {
    friend class HasLifecycle<WavSource, WavSourceConfig>;
    friend struct LifecycleContract<WavSource, WavSourceConfig>;

  public:
    DELETE_COPY(WavSource)
    DELETE_MOVE(WavSource)

    static constexpr const char *name = "Audio::WavSource";
    static constexpr LogComponent logComponent =
        Totem::Audio::detail::logComponent;

    WavSource() = default;

    [[nodiscard]] bool active() const override {
        return HasLifecycle<WavSource, WavSourceConfig>::active();
    }
    [[nodiscard]] const AudioInfo &audioInfo() const override {
        return _audioInfo;
    }
    [[nodiscard]] bool ready() const override {
        return _ready.load(std::memory_order_acquire);
    }
    [[nodiscard]] const char *sourceName() const override { return "wav"; }

    bool pollReadiness(uint32_t nowMs) override {
        if (_stream.ready()) {
            _ready.store(true, std::memory_order_release);
            return true;
        }

        if (_lastWaitingLogMs == 0 ||
            nowMs - _lastWaitingLogMs >= config().waitingLogIntervalMs) {
            _lastWaitingLogMs = nowMs;
            _log_w("Waiting for WAV source data from %s", config().path);
        }
        _ready.store(false, std::memory_order_release);
        return false;
    }

    void observeReadResult(std::size_t bytesRead, uint32_t nowMs) override {
        if (bytesRead > 0) {
            _lastWaitingLogMs = 0;
            _ready.store(true, std::memory_order_release);
            _observedBytes.fetch_add(static_cast<uint32_t>(bytesRead),
                                     std::memory_order_acq_rel);
            _lastDataMs.store(nowMs, std::memory_order_release);
            return;
        }

        if (!_stream.ready()) {
            _ready.store(false, std::memory_order_release);
        }
    }

    Platform::AudioStream &stream() override { return _stream; }

  private:
    using File = FileSystemService::DefaultFileSystem::File;

    class WavPcmStream : public Platform::AudioStream {
      public:
        DELETE_COPY(WavPcmStream)
        DELETE_MOVE(WavPcmStream)

        WavPcmStream() = default;
        ~WavPcmStream() override { end(); }

        ReturnCode begin(const WavSourceConfig &config) {
            FAIL_IF(!FileSystemService::configured(),
                    ERR(CoreError, InvalidState),
                    "Cannot start WAV source before FileSystem service is "
                    "bound");

            _config = config;
            _lastReadError = OK();
            FAIL_IF_ERR_FWD(_openPcmData(), "Failed to open WAV source %s",
                            config.path);
            Platform::setAudioInfo(*this, _audioInfo);
            _active = true;
            return OK();
        }

        void end() override {
            _active = false;
            _dataRemaining = 0;
            (void)_file.close();
        }

        [[nodiscard]] const AudioInfo &audioInfo() const { return _audioInfo; }
        [[nodiscard]] bool ready() const { return _active; }
        [[nodiscard]] ReturnCode lastReadError() const { return _lastReadError; }

        int available() override {
            if (!_active) {
                return 0;
            }
            if (_dataRemaining == 0) {
                return _config.loop ? _audioInfo.bytesPerFrame() : 0;
            }
            return static_cast<int>(
                std::min<std::size_t>(_dataRemaining,
                                      std::numeric_limits<int>::max()));
        }

        int availableForWrite() override { return 0; }
        void flush() override {}

        size_t write(const uint8_t * /*data*/, size_t /*len*/) override {
            return 0;
        }

        size_t readBytes(uint8_t *data, size_t len) override {
            if (!_active || data == nullptr || len == 0) {
                return 0;
            }

            std::size_t total = 0;
            while (total < len) {
                if (_dataRemaining == 0) {
                    if (!_config.loop) {
                        _active = false;
                        break;
                    }
                    const auto reopen = _openPcmData();
                    if (!reopen.ok()) {
                        _lastReadError = reopen;
                        _active = false;
                        break;
                    }
                }

                const auto wanted =
                    std::min<std::size_t>(len - total, _dataRemaining);
                auto read = _file.read(std::span<std::byte>{
                    reinterpret_cast<std::byte *>(data + total), wanted});
                if (!read) {
                    _lastReadError = read.error();
                    _active = false;
                    break;
                }
                if (*read == 0) {
                    _lastReadError = ERR(CoreError, InvalidData);
                    _active = false;
                    break;
                }

                total += *read;
                _dataRemaining -= *read;
            }
            return total;
        }

        operator bool() override { return _active; }

      private:
        struct ChunkHeader {
            std::array<std::byte, 4> id{};
            uint32_t size = 0;
        };

        ReturnCode _openPcmData() {
            (void)_file.close();
            FAIL_IF_ERR_FWD(
                FileSystemService::get().openRead(_file, _config.path),
                "Failed to open WAV file %s", _config.path);

            FAIL_IF_ERR_FWD(_readRiffHeader(), "Invalid WAV RIFF header");

            bool foundFormat = false;
            for (;;) {
                ChunkHeader chunk{};
                FAIL_IF_ERR_FWD(_readChunkHeader(chunk),
                                "Invalid WAV chunk header");
                if (_isChunk(chunk, "fmt ")) {
                    FAIL_IF_ERR_FWD(_readFormatChunk(chunk.size),
                                    "Unsupported WAV fmt chunk");
                    foundFormat = true;
                    continue;
                }
                if (_isChunk(chunk, "data")) {
                    FAIL_IF(!foundFormat, ERR(CoreError, InvalidData),
                            "WAV data chunk appears before fmt chunk");
                    _dataRemaining = chunk.size;
                    FAIL_IF(_dataRemaining == 0, ERR(CoreError, InvalidData),
                            "WAV data chunk is empty");
                    return OK();
                }
                FAIL_IF_ERR_FWD(_skipChunkPayload(chunk.size),
                                "Failed to skip WAV chunk");
            }
        }

        ReturnCode _readRiffHeader() {
            std::array<std::byte, 12> header{};
            FAIL_IF_ERR_FWD(_readExact(header), "Failed to read RIFF header");
            FAIL_IF(std::memcmp(header.data(), "RIFF", 4) != 0 ||
                        std::memcmp(header.data() + 8, "WAVE", 4) != 0,
                    ERR(CoreError, InvalidData),
                    "WAV file is not a RIFF/WAVE file");
            return OK();
        }

        ReturnCode _readChunkHeader(ChunkHeader &chunk) {
            std::array<std::byte, 8> header{};
            FAIL_IF_ERR_FWD(_readExact(header), "Failed to read chunk header");
            std::copy_n(header.begin(), 4, chunk.id.begin());
            chunk.size = _u32le(header.data() + 4);
            return OK();
        }

        ReturnCode _readFormatChunk(uint32_t size) {
            FAIL_IF(size < 16, ERR(CoreError, InvalidData),
                    "WAV fmt chunk is too small: %lu",
                    static_cast<unsigned long>(size));

            std::array<std::byte, 16> fmt{};
            FAIL_IF_ERR_FWD(_readExact(fmt), "Failed to read WAV fmt data");

            const auto format = _u16le(fmt.data());
            const auto channels = _u16le(fmt.data() + 2);
            const auto sampleRate = _u32le(fmt.data() + 4);
            const auto blockAlign = _u16le(fmt.data() + 12);
            const auto bitsPerSample = _u16le(fmt.data() + 14);
            FAIL_IF(format != 1, ERR(CoreError, InvalidData),
                    "Only PCM WAV files are supported, got format %u",
                    format);

            const auto bytesPerFrame =
                static_cast<uint16_t>((bitsPerSample / 8U) * channels);
            FAIL_IF(blockAlign != bytesPerFrame, ERR(CoreError, InvalidData),
                    "WAV blockAlign mismatch: %u != %u", blockAlign,
                    bytesPerFrame);

            _audioInfo = AudioInfo{
                .sampleRate = sampleRate,
                .channels = channels,
                .bitsPerSample = static_cast<uint8_t>(bitsPerSample),
            };
            FAIL_IF(!_audioInfo.validate(), ERR(CoreError, InvalidData),
                    "Unsupported WAV audio info: %lu Hz, %u ch, %u bit",
                    static_cast<unsigned long>(_audioInfo.sampleRate),
                    _audioInfo.channels, _audioInfo.bitsPerSample);

            FAIL_IF_ERR_FWD(_skipChunkPayload(size - fmt.size()),
                            "Failed to skip WAV fmt extension");
            return OK();
        }

        ReturnCode _skipChunkPayload(uint32_t size) {
            std::array<std::byte, 64> scratch{};
            std::size_t remaining = size + (size & 1U);
            while (remaining > 0) {
                const auto chunk = std::min(remaining, scratch.size());
                FAIL_IF_ERR_FWD(_readExact(std::span<std::byte>{
                                    scratch.data(), chunk}),
                                "Failed to skip WAV bytes");
                remaining -= chunk;
            }
            return OK();
        }

        ReturnCode _readExact(std::span<std::byte> out) {
            std::size_t offset = 0;
            while (offset < out.size()) {
                auto read = _file.read(out.subspan(offset));
                if (!read) {
                    return read.error();
                }
                FAIL_IF(*read == 0, ERR(CoreError, InvalidData),
                        "Unexpected end of WAV file");
                offset += *read;
            }
            return OK();
        }

        [[nodiscard]] static bool _isChunk(const ChunkHeader &chunk,
                                           const char *id) {
            return std::memcmp(chunk.id.data(), id, 4) == 0;
        }

        [[nodiscard]] static uint16_t _u16le(const std::byte *data) {
            return static_cast<uint16_t>(
                static_cast<uint16_t>(std::to_integer<uint8_t>(data[0])) |
                (static_cast<uint16_t>(std::to_integer<uint8_t>(data[1]))
                 << 8U));
        }

        [[nodiscard]] static uint32_t _u32le(const std::byte *data) {
            return static_cast<uint32_t>(
                static_cast<uint32_t>(std::to_integer<uint8_t>(data[0])) |
                (static_cast<uint32_t>(std::to_integer<uint8_t>(data[1]))
                 << 8U) |
                (static_cast<uint32_t>(std::to_integer<uint8_t>(data[2]))
                 << 16U) |
                (static_cast<uint32_t>(std::to_integer<uint8_t>(data[3]))
                 << 24U));
        }

        WavSourceConfig _config{};
        File _file{};
        AudioInfo _audioInfo{};
        std::size_t _dataRemaining = 0;
        ReturnCode _lastReadError = OK();
        bool _active = false;
    };

    ReturnCode _onBegin() {
        FAIL_IF_ERR_FWD(_stream.begin(config()), "Failed to start WAV source");
        _audioInfo = _stream.audioInfo();
        _ready.store(true, std::memory_order_release);
        _lastWaitingLogMs = 0;
        _observedBytes.store(0, std::memory_order_release);
        _lastDataMs.store(0, std::memory_order_release);
        _log_i("WAV source started: %s, %lu Hz, %u ch, %u bit%s",
               config().path, static_cast<unsigned long>(_audioInfo.sampleRate),
               _audioInfo.channels, _audioInfo.bitsPerSample,
               config().loop ? ", loop" : "");
        return OK();
    }

    ReturnCode _onEnd() {
        _ready.store(false, std::memory_order_release);
        _audioInfo = {};
        _stream.end();
        return OK();
    }

    WavPcmStream _stream{};
    AudioInfo _audioInfo{};
    uint32_t _lastWaitingLogMs = 0;
    std::atomic<bool> _ready{false};
    std::atomic<uint32_t> _observedBytes{0};
    std::atomic<uint32_t> _lastDataMs{0};
};

} // namespace Totem::Audio::detail
