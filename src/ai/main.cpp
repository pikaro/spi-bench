#include "AudioAfe/Facade.hpp"
#include "AudioSink/Facade.hpp"
#include "AudioSource/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Services/StatusLed.hpp"
#include "Setups/Core.hpp"
#include "Wifi/Facade.hpp"
#include "Wifi/detail/Commands.hpp"
#include "audio_session.hpp"
#include "config.hpp"
#include <cstddef>
#include <cstdint>

CoreSetup core{};
Totem::Wifi::Wifi wifi;
Totem::AudioSource::I2SSource micSource{};
Totem::AudioSink::I2SSink maxSink{};
Totem::AudioAfe::AfeProcessor audioAfe{core.taskRegistry};

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

class AfeMicInput {
  public:
    ReturnCode begin(Totem::AudioSource::I2SSource &source) {
        FAIL_IF(_active, ERR(CoreError, InvalidState),
                "AFE microphone input is already active");
        FAIL_IF_ERR_FWD(_observedMic.begin(source),
                        "Failed to observe mic stream");
        FAIL_IF_ERR_FWD(_pcm16.begin(_observedMic, source.audioInfo(),
                                     nemoAsrDownsamplerConfig),
                        "Failed to begin PCM16 downsampler");
        _active = true;
        return OK();
    }

    [[nodiscard]] Totem::AudioAfe::InputBinding binding() {
        return Totem::AudioAfe::InputBinding{
            .owner = this,
            .readBytes = AfeMicInput::_readBytes,
        };
    }

  private:
    static std::size_t _readBytes(void *owner, uint8_t *data,
                                  std::size_t bytes) {
        auto *self = static_cast<AfeMicInput *>(owner);
        if (self == nullptr || !self->_active || data == nullptr ||
            bytes == 0) {
            return 0;
        }
        return self->_pcm16.readBytes(data, bytes);
    }

    ObservedMicStream _observedMic{};
    AiAudio::Pcm16DownsamplerStream _pcm16{};
    bool _active = false;
};

AfeMicInput afeMicInput{};
AiAudio::WakeSession wakeSession{};

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
    ABORT_IF_ERR(afeMicInput.begin(micSource),
                 "Failed to begin AFE microphone input");
    ABORT_IF_ERR(
        wakeSession.begin(maxSink, wakeSessionConfig, delayedPlaybackConfig),
        "Failed to begin wake session");
    ABORT_IF_ERR(audioAfe.bind(afeMicInput.binding(), wakeSession.binding()),
                 "Failed to bind AI audio pipeline");
    ABORT_IF_ERR_BEGIN(audioAfe.begin(audioAfeConfig));

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
        ::platform::delay(::platform::ms_to_ticks(1));
    }
}
