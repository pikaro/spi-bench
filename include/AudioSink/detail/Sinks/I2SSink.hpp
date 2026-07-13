#pragma once

#include "AudioSink/Interfaces/SinkConfig.hpp"
#include "AudioSink/Interfaces/Types.hpp"
#include "AudioSink/detail/PlatformSelect.hpp"
#include "AudioSink/detail/Sinks/IAudioSink.hpp"
#include "AudioSink/detail/Types.hpp"
#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"

namespace Totem::AudioSink::detail {

class I2SSink : public HasLifecycle<I2SSink, I2SSinkConfig>,
                public IAudioSink {
    friend class HasLifecycle<I2SSink, I2SSinkConfig>;
    friend struct LifecycleContract<I2SSink, I2SSinkConfig>;

  public:
    DELETE_COPY(I2SSink)
    DELETE_MOVE(I2SSink)

    static constexpr const char *name = "AudioSink::I2SSink";
    static constexpr LogComponent logComponent =
        Totem::AudioSink::detail::logComponent;

    I2SSink() = default;

    [[nodiscard]] bool active() const override {
        return HasLifecycle<I2SSink, I2SSinkConfig>::active();
    }
    [[nodiscard]] const AudioInfo &audioInfo() const override {
        return _audioInfo;
    }
    [[nodiscard]] const I2SLinkConfig &linkConfig() const {
        return _linkConfig;
    }
    [[nodiscard]] bool ready() const override { return _output.active(); }
    [[nodiscard]] const char *sinkName() const override { return "i2s"; }

    Platform::AudioStream &stream() override { return _output.stream(); }

  private:
    ReturnCode _onBegin() {
        _linkConfig = this->config().resolvedLink();
        const auto &pins = this->config().pins;
        _log_i("Starting I2S output: %lu Hz, %u ch, %u bit, pins bck=%d ws=%d "
               "data=%d, hostClock=%s, format=%s, channel=%s",
               static_cast<unsigned long>(_linkConfig.audio.sampleRate),
               _linkConfig.audio.channels, _linkConfig.audio.bitsPerSample,
               static_cast<int>(pins.bitClock),
               static_cast<int>(pins.wordSelect),
               static_cast<int>(pins.dataOut),
               _hostClockRoleName(_linkConfig.hostClockRole),
               _formatName(_linkConfig.format),
               _channelName(_linkConfig.channel));
        FAIL_IF_ERR_FWD(_output.begin(pins, _linkConfig),
                        "Failed to start I2S output");
        _audioInfo = _output.audioInfo();
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = _output.end();
        _audioInfo = {};
        _linkConfig = {};
        return ret;
    }

    static constexpr const char *
    _hostClockRoleName(I2SHostClockRole hostClockRole) {
        switch (hostClockRole) {
        case I2SHostClockRole::ConsumesExternalClock:
            return "host-consumes-external-bclk-ws";
        case I2SHostClockRole::ProvidesClock:
            return "host-provides-bclk-ws";
        default:
            return "unknown";
        }
    }

    static constexpr const char *_formatName(I2SFormat format) {
        switch (format) {
        case I2SFormat::Standard:
            return "standard";
        case I2SFormat::Lsb:
            return "lsb";
        case I2SFormat::Msb:
            return "msb";
        case I2SFormat::Philips:
            return "philips";
        case I2SFormat::RightJustified:
            return "right-justified";
        case I2SFormat::LeftJustified:
            return "left-justified";
        case I2SFormat::Pcm:
            return "pcm";
        default:
            return "unknown";
        }
    }

    static constexpr const char *_channelName(I2SChannelSelect channel) {
        switch (channel) {
        case I2SChannelSelect::Stereo:
            return "stereo";
        case I2SChannelSelect::Left:
            return "left";
        case I2SChannelSelect::Right:
            return "right";
        default:
            return "unknown";
        }
    }

    Platform::I2SOutputStream _output;
    I2SLinkConfig _linkConfig{};
    AudioInfo _audioInfo{};
};

} // namespace Totem::AudioSink::detail
