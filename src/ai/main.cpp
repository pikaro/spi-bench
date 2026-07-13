#include "AudioSink/Facade.hpp"
#include "AudioSource/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Services/StatusLed.hpp"
#include "Setups/Core.hpp"
#include "Wifi/Facade.hpp"
#include "Wifi/detail/Commands.hpp"
#include "config.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

CoreSetup core{};
Totem::Wifi::Wifi wifi;
Totem::AudioSource::I2SSource micSource{};
Totem::AudioSink::I2SSink maxSink{};

namespace {

class ObservedMicStream : public audio_tools::AudioStream {
  public:
    ReturnCode begin(Totem::AudioSource::I2SSource &source) {
        FAIL_IF(_source != nullptr, ERR(CoreError, InvalidState),
                "Observed mic stream is already active");
        FAIL_IF(!source.active(), ERR(CoreError, InvalidState),
                "Cannot observe inactive mic source");

        _source = &source;
        audio_tools::AudioStream::setAudioInfo(audio_tools::AudioInfo{
            source.audioInfo().sampleRate,
            source.audioInfo().channels,
            source.audioInfo().bitsPerSample,
        });
        return OK();
    }

    void end() override { _source = nullptr; }

    size_t readBytes(uint8_t *data, size_t len) override {
        if (_source == nullptr || data == nullptr || len == 0) {
            return 0;
        }

        const auto nowMs = ::platform::get_time();
        if (!_source->ready() && !_source->pollReadiness(nowMs)) {
            return 0;
        }

        const auto bytesRead = _source->stream().readBytes(data, len);
        _source->observeReadResult(bytesRead, ::platform::get_time());
        return bytesRead;
    }

    size_t write(const uint8_t *, size_t) override { return 0; }

    int available() override {
        if (_source == nullptr || !_source->ready()) {
            return 0;
        }
        return _source->stream().available();
    }

    int availableForWrite() override { return 0; }

    void flush() override {}

    operator bool() override { return _source != nullptr; }

  private:
    Totem::AudioSource::I2SSource *_source = nullptr;
};

class DelayedMaxLoopback {
  public:
    ReturnCode begin(Totem::AudioSource::I2SSource &source,
                     Totem::AudioSink::I2SSink &sink) {
        FAIL_IF(_active, ERR(CoreError, InvalidState),
                "Delayed MAX loopback is already active");
        FAIL_IF_ERR_FWD(_observedMic.begin(source),
                        "Failed to observe mic stream");
        FAIL_IF_ERR_FWD(_pcm16.begin(_observedMic, source.audioInfo(),
                                     nemoAsrDownsamplerConfig),
                        "Failed to begin PCM16 downsampler");
        FAIL_IF(!sink.active(), ERR(CoreError, InvalidState),
                "Cannot begin MAX loopback before sink is active");

        _sink = &sink;
        _active = true;
        return OK();
    }

    ReturnCode work() {
        if (!_active) {
            return OK();
        }

        const auto writable = _sink->stream().availableForWrite();
        if (writable <= 0) {
            return OK();
        }

        const auto capacity = _pcmBuffer.size() & ~std::size_t{1};
        const auto requested =
            std::min<std::size_t>(capacity,
                                  static_cast<std::size_t>(writable)) &
            ~std::size_t{1};
        if (requested == 0) {
            return OK();
        }

        const auto bytesRead = _pcm16.readBytes(_pcmBuffer.data(), requested);
        if (bytesRead == 0) {
            return OK();
        }

        const auto processedBytes = _processDelay(bytesRead & ~std::size_t{1});
        if (processedBytes == 0) {
            return OK();
        }

        const auto written =
            _sink->stream().write(_pcmBuffer.data(), processedBytes);
        FAIL_IF(written != processedBytes, ERR(CoreError, OperationFailed),
                "MAX loopback I2S write truncated");
        return OK();
    }

  private:
    static constexpr std::size_t pcmBufferBytes = 512;
    static constexpr std::size_t delaySamples =
        AiAudio::nemoAsrPcmAudio.sampleRate;

    std::size_t _processDelay(std::size_t bytes) {
        for (std::size_t offset = 0; offset + 1U < bytes; offset += 2U) {
            const auto raw =
                static_cast<uint16_t>(_pcmBuffer[offset]) |
                (static_cast<uint16_t>(_pcmBuffer[offset + 1U]) << 8U);
            const auto sample = static_cast<int16_t>(raw);
            const auto delayed = _delayLine[_delayIndex];
            _delayLine[_delayIndex] = sample;
            _delayIndex = (_delayIndex + 1U) % _delayLine.size();
            const auto out = static_cast<uint16_t>(delayed);
            _pcmBuffer[offset] = static_cast<uint8_t>(out & 0xFFU);
            _pcmBuffer[offset + 1U] =
                static_cast<uint8_t>((out >> 8U) & 0xFFU);
        }
        return bytes;
    }

    ObservedMicStream _observedMic{};
    AiAudio::Pcm16DownsamplerStream _pcm16{};
    Totem::AudioSink::I2SSink *_sink = nullptr;
    std::array<int16_t, delaySamples> _delayLine{};
    std::array<uint8_t, pcmBufferBytes> _pcmBuffer{};
    std::size_t _delayIndex = 0;
    bool _active = false;
};

DelayedMaxLoopback loopback{};

} // namespace

void setup() {
    ABORT_IF_ERR_BEGIN(core.beginStatusLedEarly(statusLedConfig));
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    ABORT_IF_ERR_BEGIN(wifi.begin(wifiConfig));
    ABORT_IF_ERR(Totem::Wifi::Commands::registerCommands(wifi),
                 "Failed to register WiFi commands");
    ABORT_IF_ERR_BEGIN(micSource.begin(i2sAudioSourceConfig));
    ABORT_IF_ERR_BEGIN(maxSink.begin(max98357LoopbackSinkConfig));
    ABORT_IF_ERR(loopback.begin(micSource, maxSink),
                 "Failed to begin delayed MAX loopback");

    _log_i("Setup complete");
    ABORT_IF_ERR(StatusLedService::setTargetsReady(),
                 "Failed to set status LED targets-ready state");
}

extern "C" {
void app_main(void);
}

void app_main() {
    setup();
    for (;;) {
        const auto nowMs = ::platform::get_time();
        (void)core.work(nowMs);
        ABORT_IF_ERR(loopback.work(), "Delayed MAX loopback failed");
        ::platform::delay(::platform::ms_to_ticks(1));
    }
}
