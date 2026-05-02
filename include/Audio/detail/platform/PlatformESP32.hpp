// IWYU pragma: private

#pragma once

#include "esp_idf_version.h"

#include "Audio/Interfaces/Types.hpp"
#include "Audio/detail/Types.hpp"
#include "AudioTools/AudioLibs/AudioRealFFT.h"
#include "AudioTools/AudioLibs/FFT/FFTWindows.h"
#include "AudioTools/CoreAudio/AudioI2S/I2SStream.h"
#include "AudioTools/CoreAudio/StreamCopy.h"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::Audio::detail::platform {

inline audio_tools::I2SFormat toPlatformFormat(I2SFormat format) {
    switch (format) {
    case I2SFormat::Standard:
        return audio_tools::I2S_STD_FORMAT;
    case I2SFormat::Lsb:
        return audio_tools::I2S_LSB_FORMAT;
    case I2SFormat::Msb:
        return audio_tools::I2S_MSB_FORMAT;
    case I2SFormat::Philips:
        return audio_tools::I2S_PHILIPS_FORMAT;
    case I2SFormat::RightJustified:
        return audio_tools::I2S_RIGHT_JUSTIFIED_FORMAT;
    case I2SFormat::LeftJustified:
        return audio_tools::I2S_LEFT_JUSTIFIED_FORMAT;
    case I2SFormat::Pcm:
        return audio_tools::I2S_PCM;
    default:
        return audio_tools::I2S_STD_FORMAT;
    }
}

inline audio_tools::I2SChannelSelect
toPlatformChannel(I2SChannelSelect channel) {
    switch (channel) {
    case I2SChannelSelect::Stereo:
        return audio_tools::I2SChannelSelect::Stereo;
    case I2SChannelSelect::Left:
        return audio_tools::I2SChannelSelect::Left;
    case I2SChannelSelect::Right:
        return audio_tools::I2SChannelSelect::Right;
    default:
        return audio_tools::I2SChannelSelect::Default;
    }
}

inline audio_tools::AudioInfo toPlatformAudioInfo(AudioInfo info) {
    return audio_tools::AudioInfo{
        info.sampleRate,
        info.channels,
        info.bitsPerSample,
    };
}

class I2SInputStream {
  public:
    DELETE_COPY(I2SInputStream)
    DELETE_MOVE(I2SInputStream)

    I2SInputStream() = default;

    ReturnCode begin(const I2SDeviceConfig &config) {
        FAIL_IF(_active, ERR(CoreError, InvalidState),
                "I2S input stream already active");
        FAIL_IF(!config.validate(), ERR(CoreError, InvalidArgument),
                "Invalid I2S input stream config");

        auto platformConfig = _stream.defaultConfig(audio_tools::RX_MODE);
        platformConfig.sample_rate = config.audio.sampleRate;
        platformConfig.channels = config.audio.channels;
        platformConfig.bits_per_sample = config.audio.bitsPerSample;
        platformConfig.port_no = config.port;
        platformConfig.is_master = config.role == I2SRole::Master;
        platformConfig.i2s_format = toPlatformFormat(config.format);
        platformConfig.channel_format = toPlatformChannel(config.channel);
        platformConfig.pin_bck = config.pins.bitClock;
        platformConfig.pin_ws = config.pins.wordSelect;
        platformConfig.pin_data = config.pins.dataIn;
        platformConfig.pin_data_rx = config.pins.dataIn;
        platformConfig.buffer_size = config.dmaBufferSize;
        platformConfig.buffer_count = config.dmaBufferCount;
        platformConfig.use_apll = config.useApll;

        FAIL_IF(!_stream.begin(platformConfig), ERR(CoreError, OperationFailed),
                "Failed to begin I2S input stream");
        _info = config.audio;
        _active = true;
        return OK();
    }

    ReturnCode end() {
        if (!_active) {
            return OK();
        }
        _stream.end();
        _active = false;
        return OK();
    }

    [[nodiscard]] bool active() const { return _active; }
    [[nodiscard]] const AudioInfo &audioInfo() const { return _info; }

    ReturnCode setReadTimeoutMs(uint32_t timeoutMs) {
        _stream.driver()->setWaitTimeReadMs(timeoutMs);
        return OK();
    }

    size_t readBytes(uint8_t *data, size_t len) {
        if (!_active || data == nullptr || len == 0) {
            return 0;
        }
        return _stream.readBytes(data, len);
    }

    audio_tools::AudioStream &stream() { return _stream; }

  private:
    audio_tools::I2SStream _stream{};
    AudioInfo _info{};
    bool _active = false;
};

struct Platform {
    using I2SInputStream = Totem::Audio::detail::platform::I2SInputStream;
    using FftSink = audio_tools::AudioRealFFT;
    using StreamCopier = audio_tools::StreamCopy;
    using AudioFftBase = audio_tools::AudioFFTBase;
    using AudioFftConfig = audio_tools::AudioFFTConfig;
    using WindowFunction = audio_tools::WindowFunction;
    using HammingWindow = audio_tools::Hamming;
    using HannWindow = audio_tools::Hann;
};

} // namespace Totem::Audio::detail::platform
