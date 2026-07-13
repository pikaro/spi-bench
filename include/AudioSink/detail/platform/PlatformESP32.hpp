// IWYU pragma: private

#pragma once

#include "AudioSink/Interfaces/SinkConfig.hpp"
#include "AudioSink/Interfaces/Types.hpp"
#include "AudioSink/detail/Types.hpp"
#include "AudioTools/CoreAudio/AudioI2S/I2SConfig.h"
#include "AudioTools/CoreAudio/AudioI2S/I2SConfigESP32V1.h"
#include "AudioTools/CoreAudio/AudioI2S/I2SESP32V1.h"
#include "AudioTools/CoreAudio/AudioTypes.h"
#include "AudioTools/CoreAudio/BaseStream.h"
#include "Macros/Facade.hpp"
#include "Network/detail/TcpSocket.hpp"
#include "SecretStorage/Interfaces/Secret.hpp"
#include "Types/Error.hpp"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ws.h"
#include "lwip/inet.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <utility>

namespace Totem::AudioSink::detail::platform {

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

    size_t readBytes(uint8_t *, size_t) override { return 0; }

    int available() override { return 0; }

    int availableForWrite() override {
        return _active ? I2S_BUFFER_COUNT * I2S_BUFFER_SIZE : 0;
    }

    void flush() override {}

    audio_tools::I2SDriverESP32V1 *driver() { return &_driver; }

    operator bool() override { return _active; }

    [[nodiscard]] bool isActive() const { return _active; }

  private:
    audio_tools::I2SDriverESP32V1 _driver{};
    bool _active = false;
};

class I2SOutputStream {
  public:
    DELETE_COPY(I2SOutputStream)
    DELETE_MOVE(I2SOutputStream)

    I2SOutputStream() = default;

    ReturnCode begin(const I2SOutputPins &pins, const I2SLinkConfig &config) {
        FAIL_IF(_active, ERR(CoreError, InvalidState),
                "I2S output stream already active");
        FAIL_IF(!config.validate(), ERR(CoreError, InvalidArgument),
                "Invalid I2S output stream config");

        auto platformConfig = _stream.defaultConfig(audio_tools::TX_MODE);
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
        platformConfig.pin_data = static_cast<int>(pins.dataOut);
        platformConfig.pin_data_rx = I2S_GPIO_UNUSED;
        platformConfig.use_apll = config.useApll;

        FAIL_IF(!_stream.begin(platformConfig), ERR(CoreError, OperationFailed),
                "Failed to begin I2S output stream");
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

    ReturnCode setWriteTimeoutMs(uint32_t timeoutMs) {
        _stream.driver()->setWaitTimeWriteMs(timeoutMs);
        return OK();
    }

    audio_tools::AudioStream &stream() { return _stream; }

  private:
    I2SAudioStream _stream{};
    AudioInfo _info{};
    bool _active = false;
};

class TcpOutputStream : public audio_tools::AudioStream {
  public:
    DELETE_COPY(TcpOutputStream)
    DELETE_MOVE(TcpOutputStream)

    TcpOutputStream() = default;
    ~TcpOutputStream() override { (void)close(); }

    ReturnCode begin(const NetworkSinkConfig &config) {
        FAIL_IF(_active, ERR(CoreError, InvalidState),
                "TCP output stream already active");
        FAIL_IF(!config.validateEndpointOnly(), ERR(CoreError, InvalidArgument),
                "Invalid TCP output stream config");
        _config = config;
        setAudioInfo(toPlatformAudioInfo(_config.audio));
        _ready = false;
        _reconnects = 0;
        _writeFailures = 0;
        _writtenBytes = 0;
        _lastConnectAttemptMs = 0;
        _active = true;
        return OK();
    }

    void end() override { (void)close(); }

    ReturnCode close() {
        (void)_connection.close();
        _ready = false;
        _active = false;
        return OK();
    }

    size_t write(const uint8_t *data, size_t len) override {
        if (!_active || data == nullptr || len == 0) {
            return 0;
        }
        if (!_ensureConnected()) {
            return 0;
        }

        auto bytes = std::as_bytes(std::span<const uint8_t>{data, len});
        const auto ret = _connection.sendAll(bytes);
        if (!ret.ok()) {
            _writeFailures += 1;
            _ready = false;
            (void)_connection.close();
            return 0;
        }

        _writtenBytes += static_cast<uint32_t>(
            std::min<std::size_t>(len, UINT32_MAX - _writtenBytes));
        return len;
    }

    size_t readBytes(uint8_t *, size_t) override { return 0; }

    int available() override { return 0; }

    int availableForWrite() override { return _active ? 1024 : 0; }

    void flush() override {}

    [[nodiscard]] bool active() const { return _active; }
    [[nodiscard]] bool ready() const { return _ready; }
    [[nodiscard]] const AudioInfo &audioInfo() const { return _config.audio; }

    [[nodiscard]] AudioSinkStatus status() const {
        return AudioSinkStatus{
            .ready = _ready,
            .reconnects = _reconnects,
            .writeFailures = _writeFailures,
            .writtenBytes = _writtenBytes,
        };
    }

  private:
    bool _ensureConnected() {
        if (_ready) {
            return true;
        }

        const auto nowMs = ::platform::get_time();
        if (_lastConnectAttemptMs != 0 &&
            nowMs - _lastConnectAttemptMs < _config.reconnectIntervalMs) {
            return false;
        }
        _lastConnectAttemptMs = nowMs;

        (void)_connection.close();
        const auto ret = _client.connectTo(
            _config.endpoint, _config.connectTimeoutMs, _connection);
        if (!ret.ok()) {
            return false;
        }

        _ready = true;
        _reconnects += 1;
        return true;
    }

    NetworkSinkConfig _config{};
    Network::detail::DefaultTcpClient _client{};
    Network::detail::DefaultTcpConnection _connection{};
    uint32_t _lastConnectAttemptMs = 0;
    uint32_t _reconnects = 0;
    uint32_t _writeFailures = 0;
    uint32_t _writtenBytes = 0;
    bool _active = false;
    bool _ready = false;
};

class WebSocketOutputStream : public audio_tools::AudioStream {
  public:
    DELETE_COPY(WebSocketOutputStream)
    DELETE_MOVE(WebSocketOutputStream)

    WebSocketOutputStream() = default;
    ~WebSocketOutputStream() override { (void)close(); }

    ReturnCode begin(const WebSocketSinkConfig &config) {
        FAIL_IF(_active, ERR(CoreError, InvalidState),
                "WebSocket output stream already active");
        FAIL_IF(!config.validate(), ERR(CoreError, InvalidArgument),
                "Invalid WebSocket output stream config");
        _config = config;
        setAudioInfo(toPlatformAudioInfo(_config.network.audio));
        FAIL_IF_ERR_FWD(_prepareHost(), "Failed to prepare WebSocket host");
        FAIL_IF_ERR_FWD(_prepareAuth(), "Failed to prepare WebSocket auth");
        _ready = false;
        _reconnects = 0;
        _writeFailures = 0;
        _writtenBytes = 0;
        _lastConnectAttemptMs = 0;
        _active = true;
        return OK();
    }

    void end() override { (void)close(); }

    ReturnCode close() {
        _destroyTransport();
        _ready = false;
        _active = false;
        return OK();
    }

    size_t write(const uint8_t *data, size_t len) override {
        if (!_active || data == nullptr || len == 0) {
            return 0;
        }
        if (!_ensureConnected()) {
            return 0;
        }

        std::size_t offset = 0;
        while (offset < len) {
            const auto chunk =
                std::min<std::size_t>(_config.packetBytes, len - offset);
            std::memcpy(_packetBuffer.data(), data + offset, chunk);
            const auto written = esp_transport_write(
                _webSocket,
                reinterpret_cast<const char *>(_packetBuffer.data()),
                static_cast<int>(chunk), _config.network.writeTimeoutMs);
            if (written != static_cast<int>(chunk)) {
                _writeFailures += 1;
                _ready = false;
                _destroyTransport();
                return offset;
            }
            offset += chunk;
        }

        _writtenBytes += static_cast<uint32_t>(
            std::min<std::size_t>(len, UINT32_MAX - _writtenBytes));
        return len;
    }

    size_t readBytes(uint8_t *, size_t) override { return 0; }

    int available() override { return 0; }

    int availableForWrite() override {
        return _active ? static_cast<int>(_config.packetBytes) : 0;
    }

    void flush() override {}

    [[nodiscard]] bool active() const { return _active; }
    [[nodiscard]] bool ready() const { return _ready; }
    [[nodiscard]] const AudioInfo &audioInfo() const {
        return _config.network.audio;
    }

    [[nodiscard]] AudioSinkStatus status() const {
        return AudioSinkStatus{
            .ready = _ready,
            .reconnects = _reconnects,
            .writeFailures = _writeFailures,
            .writtenBytes = _writtenBytes,
        };
    }

  private:
    ReturnCode _prepareHost() {
        if (_config.network.hasHostName()) {
            return OK();
        }

        in_addr address{.s_addr = _config.network.endpoint.address};
        FAIL_IF(lwip_inet_ntop(AF_INET, &address, _hostBuffer.data(),
                               _hostBuffer.size()) == nullptr,
                ERR(CoreError, InvalidArgument),
                "Failed to format WebSocket endpoint host");
        return OK();
    }

    ReturnCode _prepareAuth() {
        _authHeader[0] = '\0';
        if (!_config.hasBearerToken()) {
            return OK();
        }
        auto tokenSecret =
            SecretStorage::Secret<webSocketMaxBearerTokenBytes, 1>{
                _config.bearerTokenSecretName};
        FAIL_IF_ERR_FWD(tokenSecret.read(),
                        "Failed to read WebSocket bearer token secret");
        const auto ret = std::snprintf(
            _authHeader.data(), _authHeader.size(), "Bearer %.*s",
            static_cast<int>(tokenSecret.size()),
            reinterpret_cast<const char *>(tokenSecret.view().data()));
        FAIL_IF(ret < 0 || static_cast<std::size_t>(ret) >= _authHeader.size(),
                ERR(CoreError, InvalidSize),
                "WebSocket bearer token is too long");
        return OK();
    }

    [[nodiscard]] const char *_host() const {
        return _config.network.hasHostName() ? _config.network.hostName
                                             : _hostBuffer.data();
    }

    bool _ensureConnected() {
        if (_ready) {
            return true;
        }

        const auto nowMs = ::platform::get_time();
        if (_lastConnectAttemptMs != 0 &&
            nowMs - _lastConnectAttemptMs <
                _config.network.reconnectIntervalMs) {
            return false;
        }
        _lastConnectAttemptMs = nowMs;

        _destroyTransport();
        if (!_createTransport()) {
            return false;
        }

        const auto connectRet = esp_transport_connect(
            _webSocket, _host(), _config.network.endpoint.port,
            _config.network.connectTimeoutMs);
        if (connectRet != 0) {
            _destroyTransport();
            return false;
        }

        _ready = true;
        _reconnects += 1;
        return true;
    }

    bool _createTransport() {
        _parent = _config.secure ? esp_transport_ssl_init()
                                 : esp_transport_tcp_init();
        if (_parent == nullptr) {
            return false;
        }

        if (_config.secure) {
            esp_transport_ssl_set_cert_data(
                _parent, _config.trustedRootPem,
                static_cast<int>(std::strlen(_config.trustedRootPem) + 1U));
        }

        _webSocket = esp_transport_ws_init(_parent);
        if (_webSocket == nullptr) {
            _destroyTransport();
            return false;
        }

        esp_transport_ws_set_path(_webSocket, _config.path);
        if (_config.hasBearerToken()) {
            const auto authRet =
                esp_transport_ws_set_auth(_webSocket, _authHeader.data());
            if (authRet != ESP_OK) {
                _destroyTransport();
                return false;
            }
        }
        return true;
    }

    void _destroyTransport() {
        if (_webSocket != nullptr) {
            const auto parent = _parent;
            (void)esp_transport_close(_webSocket);
            (void)esp_transport_destroy(_webSocket);
            _webSocket = nullptr;
            _parent = nullptr;
            if (parent != nullptr) {
                (void)esp_transport_destroy(parent);
            }
            return;
        }
        if (_parent != nullptr) {
            (void)esp_transport_close(_parent);
            (void)esp_transport_destroy(_parent);
            _parent = nullptr;
        }
    }

    WebSocketSinkConfig _config{};
    std::array<char, 16> _hostBuffer{};
    std::array<char, webSocketMaxBearerTokenBytes + 8> _authHeader{};
    std::array<uint8_t, webSocketMaxPacketBytes> _packetBuffer{};
    esp_transport_handle_t _parent = nullptr;
    esp_transport_handle_t _webSocket = nullptr;
    uint32_t _lastConnectAttemptMs = 0;
    uint32_t _reconnects = 0;
    uint32_t _writeFailures = 0;
    uint32_t _writtenBytes = 0;
    bool _active = false;
    bool _ready = false;
};

struct Platform {
    using AudioStream = audio_tools::AudioStream;
    using I2SOutputStream = Totem::AudioSink::detail::platform::I2SOutputStream;
    using TcpOutputStream = Totem::AudioSink::detail::platform::TcpOutputStream;
    using WebSocketOutputStream =
        Totem::AudioSink::detail::platform::WebSocketOutputStream;

    static void setAudioInfo(AudioStream &stream, AudioInfo info) {
        stream.setAudioInfo(toPlatformAudioInfo(info));
    }
};

} // namespace Totem::AudioSink::detail::platform
