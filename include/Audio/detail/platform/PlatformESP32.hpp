// IWYU pragma: private

#pragma once

#include "Audio/Interfaces/SourceConfig.hpp"
#include "AudioTools/AudioLibs/AudioFFT.h"
#include "AudioTools/CoreAudio/AudioI2S/I2SConfigESP32V1.h"
#include "AudioTools/CoreAudio/AudioI2S/I2SESP32V1.h"
#include "AudioTools/CoreAudio/AudioTypes.h"

#include "Audio/Interfaces/Types.hpp"
#include "AudioTools/AudioLibs/AudioEspressifFFT.h"
#include "AudioTools/AudioLibs/AudioRealFFT.h"
#include "AudioTools/AudioLibs/FFT/FFTWindows.h"
#include "AudioTools/CoreAudio/AudioI2S/I2SConfig.h"
#include "AudioTools/CoreAudio/AudioStreams.h"
#include "AudioTools/CoreAudio/BaseStream.h"
#include "AudioTools/CoreAudio/StreamCopy.h"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
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

class I2SDriverWithReadStatus : public audio_tools::I2SDriverESP32V1 {
  public:
    esp_err_t readBytesWithStatus(void *dest, size_t sizeBytes,
                                  size_t &bytesRead) {
        bytesRead = 0;
        if (dest == nullptr || sizeBytes == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        if (rx_chan == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        return i2s_channel_read(rx_chan, dest, sizeBytes, &bytesRead,
                                ticks_to_wait_read);
    }
};

class I2SAudioStream : public audio_tools::AudioStream {
  public:
    audio_tools::I2SConfig defaultConfig(audio_tools::RxTxMode mode) {
        return _driver.defaultConfig(mode);
    }

    bool begin(audio_tools::I2SConfig config) {
        if (!config) {
            return false;
        }
        audio_tools::AudioStream::setAudioInfo(config);
        _lastReadStatus = ESP_OK;
        _lastReadBytes = 0;
        _active = _driver.begin(config);
        return _active;
    }

    void end() {
        if (!_active) {
            return;
        }
        _active = false;
        _driver.end();
    }

    void setAudioInfo(audio_tools::AudioInfo info) override {
        audio_tools::AudioStream::setAudioInfo(info);
        if (_active) {
            auto config = _driver.config();
            config.copyFrom(info);
            (void)_driver.setAudioInfo(config);
        }
    }

    size_t write(const uint8_t *data, size_t len) override {
        if (!_active || data == nullptr || len == 0) {
            return 0;
        }
        return _driver.writeBytes(data, len);
    }

    size_t readBytes(uint8_t *data, size_t len) override {
        if (!_active || data == nullptr || len == 0) {
            _lastReadStatus = ESP_ERR_INVALID_STATE;
            _lastReadBytes = 0;
            return 0;
        }
        _lastReadStatus =
            _driver.readBytesWithStatus(data, len, _lastReadBytes);
        return _lastReadBytes;
    }

    int available() override {
        return _active ? I2S_BUFFER_COUNT * I2S_BUFFER_SIZE : 0;
    }

    int availableForWrite() override {
        return _active ? I2S_BUFFER_COUNT * I2S_BUFFER_SIZE : 0;
    }

    void flush() override {}

    I2SDriverWithReadStatus *driver() { return &_driver; }

    operator bool() override { return _active; }

    [[nodiscard]] bool isActive() const { return _active; }
    [[nodiscard]] esp_err_t lastReadStatus() const { return _lastReadStatus; }
    [[nodiscard]] size_t lastReadBytes() const { return _lastReadBytes; }

  private:
    I2SDriverWithReadStatus _driver{};
    esp_err_t _lastReadStatus = ESP_OK;
    size_t _lastReadBytes = 0;
    bool _active = false;
};

class I2SInputStream {
  public:
    DELETE_COPY(I2SInputStream)
    DELETE_MOVE(I2SInputStream)

    I2SInputStream() = default;

    ReturnCode begin(const I2SPins &pins, const I2SLinkConfig &config) {
        FAIL_IF(_active, ERR(CoreError, InvalidState),
                "I2S input stream already active");
        FAIL_IF(!config.validate(), ERR(CoreError, InvalidArgument),
                "Invalid I2S input stream config");

        auto platformConfig = _stream.defaultConfig(audio_tools::RX_MODE);
        platformConfig.sample_rate = config.audio.sampleRate;
        platformConfig.channels = config.audio.channels;
        platformConfig.bits_per_sample = config.audio.bitsPerSample;
        platformConfig.port_no = config.port;
        platformConfig.is_master =
            config.hostClockRole == I2SHostClockRole::ProvidesClock;
        platformConfig.i2s_format = toPlatformFormat(config.format);
        platformConfig.channel_format = toPlatformChannel(config.channel);
        platformConfig.pin_bck = static_cast<int>(pins.bitClock);
        platformConfig.pin_ws = static_cast<int>(pins.wordSelect);
        platformConfig.pin_data = static_cast<int>(pins.dataIn);
        platformConfig.pin_data_rx = static_cast<int>(pins.dataIn);
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

    [[nodiscard]] bool lastReadOk() const {
        return _stream.lastReadStatus() == ESP_OK;
    }

    [[nodiscard]] bool lastReadTimedOut() const {
        return _stream.lastReadStatus() == ESP_ERR_TIMEOUT;
    }

    [[nodiscard]] int32_t lastReadStatusCode() const {
        return static_cast<int32_t>(_stream.lastReadStatus());
    }

    [[nodiscard]] const char *lastReadStatusName() const {
        return esp_err_to_name(_stream.lastReadStatus());
    }

    audio_tools::AudioStream &stream() { return _stream; }

  private:
    I2SAudioStream _stream{};
    AudioInfo _info{};
    bool _active = false;
};

struct Platform {
    using AudioStream = audio_tools::AudioStream;
    using I2SInputStream = Totem::Audio::detail::platform::I2SInputStream;
    using RealFftSink = audio_tools::AudioRealFFT;
    using EspressifFftSink = audio_tools::AudioEspressifFFT;
    using StreamCopier = audio_tools::StreamCopy;
    using AudioFftBase = audio_tools::AudioFFTBase;
    using AudioFftConfig = audio_tools::AudioFFTConfig;
    using WindowFunction = audio_tools::WindowFunction;
    using HammingWindow = audio_tools::Hamming;
    using HannWindow = audio_tools::Hann;

    static void setAudioInfo(AudioStream &stream, AudioInfo info) {
        stream.setAudioInfo(toPlatformAudioInfo(info));
    }
};

} // namespace Totem::Audio::detail::platform
