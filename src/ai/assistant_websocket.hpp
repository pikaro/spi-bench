#pragma once

#include "AudioSink/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "SecretStorage/Interfaces/Secret.hpp"
#include "Types/Error.hpp"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include "mbedtls/base64.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string_view>

namespace AiAudio {

inline constexpr std::size_t assistantVoiceMaximumBytes = 32;

[[nodiscard]] constexpr bool validAssistantVoice(std::string_view voice) {
    if (voice.empty() || voice.size() > assistantVoiceMaximumBytes) {
        return false;
    }
    for (const auto character : voice) {
        const bool alphaNumeric =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9');
        if (!alphaNumeric && character != '-' && character != '_') {
            return false;
        }
    }
    return true;
}

struct AssistantWebSocketConfig {
    const char *host = nullptr;
    uint16_t port = 443;
    const char *path = "/v1/realtime";
    const char *authorizationHeaderSecretName = nullptr;
    const char *trustedRootPem = nullptr;
    std::size_t appendPcmBytes = 2048;
    std::size_t maximumInboundEventBytes = 128U * 1024U;
    std::size_t maximumDecodedAudioBytes = 96U * 1024U;
    uint32_t connectTimeoutMs = 10000;
    uint32_t writeTimeoutMs = 3000;
    uint32_t readTimeoutMs = 250;

    [[nodiscard]] bool validate() const {
        const auto secretNameLength =
            authorizationHeaderSecretName == nullptr
                ? 0U
                : std::strlen(authorizationHeaderSecretName);
        const auto maximumDecoded = (maximumInboundEventBytes / 4U) * 3U;
        return host != nullptr && host[0] != '\0' && port != 0 &&
               path != nullptr && path[0] == '/' &&
               authorizationHeaderSecretName != nullptr &&
               secretNameLength > 0 &&
               secretNameLength <=
                   Totem::AudioSink::webSocketMaxAuthorizationSecretNameBytes &&
               trustedRootPem != nullptr && trustedRootPem[0] != '\0' &&
               appendPcmBytes > 0 && appendPcmBytes % sizeof(int16_t) == 0 &&
               maximumInboundEventBytes >= 1024U &&
               maximumInboundEventBytes <=
                   static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
               maximumDecodedAudioBytes >= maximumDecoded &&
               connectTimeoutMs > 0 && writeTimeoutMs > 0 && readTimeoutMs > 0;
    }
};

enum class AssistantServerEventType : uint8_t {
    None,
    Unknown,
    Error,
    AudioStarted,
    AudioDelta,
    AudioDone,
    ResponseDone,
    Closed,
};

struct AssistantServerEvent {
    AssistantServerEventType type = AssistantServerEventType::None;
    uint32_t sampleRate = 0;
    uint8_t sampleWidth = 0;
    uint8_t channels = 0;
    uint16_t closeCode = 0;
    std::span<std::byte> audio{};
};

class AssistantWebSocket {
  public:
    DELETE_COPY(AssistantWebSocket)
    DELETE_MOVE(AssistantWebSocket)

    static constexpr uint16_t normalClosureCode = 1000;
    static constexpr uint16_t policyViolationCode = 1008;

    AssistantWebSocket() = default;
    ~AssistantWebSocket() { end(); }

    ReturnCode begin(AssistantWebSocketConfig config) {
        FAIL_IF(_active, ERR(CoreError, InvalidState),
                "Assistant WebSocket is already active");
        FAIL_IF_NOT(config.validate(), ERR(CoreError, InvalidArgument),
                    "Invalid assistant WebSocket config");

        _config = config;
        // The turn task has a PSRAM-backed stack. Keep NVS reads on the setup
        // task because NVS temporarily disables the flash cache.
        FAIL_IF_ERR_FWD(_loadAuthorizationHeader(),
                        "Assistant authorization header preflight failed");
        const auto base64Bytes = _base64Size(config.appendPcmBytes);
        _outboundCapacity =
            appendPrefix.size() + base64Bytes + appendSuffix.size() + 1U;
        _outbound = _allocate(_outboundCapacity);
        _message = _allocate(config.maximumInboundEventBytes + 1U);
        _decoded = _allocate(config.maximumDecodedAudioBytes);
        _readBuffer = _allocate(readBufferBytes);
        if (_outbound == nullptr || _message == nullptr ||
            _decoded == nullptr || _readBuffer == nullptr) {
            _freeBuffers();
            FAIL(ERR(CoreError, OutOfMemory),
                 "Failed to allocate assistant WebSocket buffers in PSRAM");
        }

        cJSON_Hooks hooks{
            .malloc_fn = AssistantWebSocket::_jsonAllocate,
            .free_fn = heap_caps_free,
        };
        cJSON_InitHooks(&hooks);
        esp_log_level_set("transport_ws", ESP_LOG_INFO);
        _active = true;
        return OK();
    }

    void end() {
        disconnect();
        _freeBuffers();
        _active = false;
    }

    [[nodiscard]] bool connected() const { return _connected; }

    ReturnCode connect(uint32_t wakeTimestampMs, int64_t wakeEpochMs) {
        FAIL_IF(!_active, ERR(CoreError, InvalidState),
                "Assistant WebSocket is not active");
        FAIL_IF(_connected, ERR(CoreError, InvalidState),
                "Assistant WebSocket is already connected");
        FAIL_IF(wakeEpochMs <= 0, ERR(CoreError, InvalidArgument),
                "Assistant request timestamp is invalid");

        std::array<char, requestHeadersCapacity> requestHeaders{};
        const auto requestHeadersLength =
            std::snprintf(requestHeaders.data(), requestHeaders.size(),
                          "X-Request-Id: wake-%" PRIu32 "\r\n"
                          "X-Request-Timestamp: %" PRId64 "\r\n",
                          wakeTimestampMs, wakeEpochMs);
        FAIL_IF(requestHeadersLength < 0 ||
                    static_cast<std::size_t>(requestHeadersLength) >=
                        requestHeaders.size(),
                ERR(CoreError, Overflow),
                "Assistant request correlation headers are too large");

        _parent = esp_transport_ssl_init();
        FAIL_IF_NULL(_parent, ERR(CoreError, OutOfMemory),
                     "Failed to allocate assistant TLS transport");
        esp_transport_ssl_set_cert_data(
            _parent, _config.trustedRootPem,
            static_cast<int>(std::strlen(_config.trustedRootPem)));

        _webSocket = esp_transport_ws_init(_parent);
        if (_webSocket == nullptr) {
            disconnect();
            FAIL(ERR(CoreError, OutOfMemory),
                 "Failed to allocate assistant WebSocket transport");
        }

        const esp_transport_ws_config_t transportConfig{
            .ws_path = _config.path,
            .sub_protocol = nullptr,
            .user_agent = "Totem-AI/1",
            .headers = requestHeaders.data(),
            .auth = _authorizationHeader.data(),
            .propagate_control_frames = true,
        };
        const auto configResult =
            esp_transport_ws_set_config(_webSocket, &transportConfig);
        if (configResult != ESP_OK) {
            disconnect();
            FAIL(ERR(CoreError, OperationFailed),
                 "Failed to configure assistant WebSocket transport");
        }

        const auto connectResult =
            esp_transport_connect(_webSocket, _config.host, _config.port,
                                  static_cast<int>(_config.connectTimeoutMs));
        const auto status =
            esp_transport_ws_get_upgrade_request_status(_webSocket);
        if (connectResult != 0 || status != 101) {
            disconnect();
            FAIL(ERR(CoreError, OperationFailed),
                 "Assistant WebSocket upgrade failed with HTTP status %d",
                 status);
        }

        const auto socketResult = _configureLowLatencySocket();
        if (!socketResult.ok()) {
            disconnect();
            FAIL_ERR_FWD(socketResult,
                         "Failed to configure assistant WebSocket socket");
        }

        _connected = true;
        _resetReadState();
        return OK();
    }

    ReturnCode sendSessionUpdate(std::string_view voice) {
        FAIL_IF(!_connected, ERR(CoreError, InvalidState),
                "Cannot update a disconnected assistant session");
        FAIL_IF_NOT(validAssistantVoice(voice), ERR(CoreError, InvalidArgument),
                    "Invalid assistant voice");

        const auto messageBytes =
            sessionUpdatePrefix.size() + voice.size() +
            sessionUpdateSuffix.size();
        FAIL_IF(messageBytes >= _outboundCapacity, ERR(CoreError, Overflow),
                "Assistant session update exceeds the outbound buffer");

        auto *cursor = _outbound;
        std::memcpy(cursor, sessionUpdatePrefix.data(),
                    sessionUpdatePrefix.size());
        cursor += sessionUpdatePrefix.size();
        std::memcpy(cursor, voice.data(), voice.size());
        cursor += voice.size();
        std::memcpy(cursor, sessionUpdateSuffix.data(),
                    sessionUpdateSuffix.size());
        _outbound[messageBytes] = '\0';
        return _sendText(_outbound, messageBytes, "session update");
    }

    ReturnCode sendAudio(std::span<const std::byte> pcm) {
        FAIL_IF(!_connected, ERR(CoreError, InvalidState),
                "Cannot send audio on a disconnected assistant WebSocket");
        FAIL_IF(pcm.empty() || pcm.size() > _config.appendPcmBytes ||
                    pcm.size() % sizeof(int16_t) != 0,
                ERR(CoreError, InvalidSize),
                "Invalid assistant PCM append size");

        std::memcpy(_outbound, appendPrefix.data(), appendPrefix.size());
        std::size_t encodedBytes = 0;
        // mbedTLS also needs room for its temporary trailing NUL. The JSON
        // suffix overwrites that byte before the complete message is sent.
        const auto encodeResult = mbedtls_base64_encode(
            reinterpret_cast<unsigned char *>(_outbound + appendPrefix.size()),
            _outboundCapacity - appendPrefix.size() - appendSuffix.size(),
            &encodedBytes, reinterpret_cast<const unsigned char *>(pcm.data()),
            pcm.size());
        FAIL_IF(encodeResult != 0, ERR(CoreError, InvalidData),
                "Failed to base64-encode assistant PCM");

        const auto suffixOffset = appendPrefix.size() + encodedBytes;
        std::memcpy(_outbound + suffixOffset, appendSuffix.data(),
                    appendSuffix.size());
        const auto messageBytes = suffixOffset + appendSuffix.size();
        _outbound[messageBytes] = '\0';
        return _sendText(_outbound, messageBytes, "audio append");
    }

    ReturnCode sendCommit() { return _sendFixed(commit, "input commit"); }

    ReturnCode receive(AssistantServerEvent &event, uint32_t timeoutMs) {
        event = {};
        FAIL_IF(!_connected, ERR(CoreError, InvalidState),
                "Cannot receive on a disconnected assistant WebSocket");

        const auto startedAtMs = ::platform::get_time();
        for (;;) {
            const auto elapsedMs =
                static_cast<uint32_t>(::platform::get_time() - startedAtMs);
            if (elapsedMs >= timeoutMs) {
                return OK();
            }
            const auto remainingTimeoutMs = timeoutMs - elapsedMs;
            const bool newFrame = _frameRemaining == 0;
            const auto read = esp_transport_read(
                _webSocket, _readBuffer, static_cast<int>(readBufferBytes),
                static_cast<int>(remainingTimeoutMs));
            FAIL_IF(read < 0, ERR(CoreError, OperationFailed),
                    "Failed to read assistant WebSocket frame");

            if (newFrame) {
                _frameOpcode = esp_transport_ws_get_read_opcode(_webSocket);
                if (_frameOpcode == WS_TRANSPORT_OPCODES_NONE) {
                    FAIL_IF(read != 0, ERR(CoreError, InvalidData),
                            "Assistant WebSocket read has no frame opcode");
                    return OK();
                }
                const auto payloadBytes =
                    esp_transport_ws_get_read_payload_len(_webSocket);
                FAIL_IF(payloadBytes < 0, ERR(CoreError, InvalidData),
                        "Assistant WebSocket frame has an invalid size");
                _frameRemaining = static_cast<std::size_t>(payloadBytes);
                _frameFinal = esp_transport_ws_get_fin_flag(_webSocket);
                FAIL_IF(_isControl(_frameOpcode) && _frameRemaining > 125U,
                        ERR(CoreError, InvalidData),
                        "Assistant WebSocket control frame is oversized");
                FAIL_IF(!_isControl(_frameOpcode) &&
                            _frameRemaining > _config.maximumInboundEventBytes,
                        ERR(CoreError, Overflow),
                        "Assistant WebSocket frame exceeds the event limit");
                FAIL_IF_ERR_FWD(_beginFrame(),
                                "Invalid assistant WebSocket frame sequence");
            }

            const auto readBytes = static_cast<std::size_t>(read);
            FAIL_IF(readBytes > _frameRemaining, ERR(CoreError, Overflow),
                    "Assistant WebSocket returned excess frame bytes");
            _frameRemaining -= readBytes;

            if (_isControl(_frameOpcode)) {
                if (_frameRemaining != 0) {
                    continue;
                }
                FAIL_IF_ERR_FWD(_finishControlFrame(event, readBytes),
                                "Failed to handle assistant control frame");
                if (event.type != AssistantServerEventType::None) {
                    return OK();
                }
                continue;
            }

            FAIL_IF(_messageSize + readBytes > _config.maximumInboundEventBytes,
                    ERR(CoreError, Overflow),
                    "Assistant WebSocket message exceeds the event limit");
            if (readBytes > 0) {
                std::memcpy(_message + _messageSize, _readBuffer, readBytes);
                _messageSize += readBytes;
            }
            if (_frameRemaining != 0 || !_frameFinal) {
                continue;
            }

            _message[_messageSize] = '\0';
            const auto parseResult = _parseMessage(event);
            _messageStarted = false;
            _messageSize = 0;
            FAIL_IF_ERR_FWD(parseResult,
                            "Failed to parse assistant WebSocket event");
            return OK();
        }
    }

    void close(uint16_t code = normalClosureCode) {
        if (_webSocket != nullptr && _connected) {
            _outbound[0] = static_cast<char>(code >> 8U);
            _outbound[1] = static_cast<char>(code & 0xFFU);
            (void)esp_transport_ws_send_raw(
                _webSocket, _opcodeWithFin(WS_TRANSPORT_OPCODES_CLOSE),
                _outbound, 2, static_cast<int>(_config.writeTimeoutMs));
        }
        disconnect();
    }

    void disconnect() {
        _connected = false;
        _resetReadState();
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

  private:
    static constexpr std::string_view sessionUpdatePrefix =
        R"({"type":"session.update","session":{"input_audio_sample_rate":16000,"input_audio_channels":1,"voice":")";
    static constexpr std::string_view sessionUpdateSuffix = R"("}})";
    static constexpr std::string_view appendPrefix =
        R"({"type":"input_audio_buffer.append","audio":")";
    static constexpr std::string_view appendSuffix = R"("})";
    static constexpr std::string_view commit =
        R"({"type":"input_audio_buffer.commit"})";
    static constexpr std::size_t readBufferBytes = 4096;
    static constexpr std::size_t requestHeadersCapacity = 96;

    using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

    static char *_allocate(std::size_t bytes) {
        return static_cast<char *>(
            heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }

    static void *_jsonAllocate(std::size_t bytes) {
        return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }

    static constexpr std::size_t _base64Size(std::size_t bytes) {
        return ((bytes + 2U) / 3U) * 4U;
    }

    ReturnCode _configureLowLatencySocket() const {
        const auto socket = esp_transport_get_socket(_webSocket);
        FAIL_IF(socket < 0, ERR(CoreError, InvalidState),
                "Assistant WebSocket has no connected socket");

        constexpr int enabled = 1;
        if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &enabled,
                       sizeof(enabled)) != 0) {
            const auto socketError = errno;
            FAIL(ERR(CoreError, OperationFailed),
                 "Failed to enable TCP_NODELAY: errno=%d", socketError);
        }
        return OK();
    }

    static constexpr bool _isControl(ws_transport_opcodes_t opcode) {
        return opcode == WS_TRANSPORT_OPCODES_CLOSE ||
               opcode == WS_TRANSPORT_OPCODES_PING ||
               opcode == WS_TRANSPORT_OPCODES_PONG;
    }

    static constexpr ws_transport_opcodes_t
    _opcodeWithFin(ws_transport_opcodes_t opcode) {
        return static_cast<ws_transport_opcodes_t>(
            static_cast<unsigned>(opcode) |
            static_cast<unsigned>(WS_TRANSPORT_OPCODES_FIN));
    }

    ReturnCode _loadAuthorizationHeader() {
        _authorizationHeader.fill('\0');
        auto secret = Totem::SecretStorage::Secret<
            Totem::AudioSink::webSocketMaxAuthorizationHeaderBytes, 1>{
            _config.authorizationHeaderSecretName};
        FAIL_IF_ERR_FWD(secret.read(),
                        "Failed to read assistant authorization header secret");
        const auto value = secret.view();
        const auto invalid = std::ranges::any_of(value, [](std::byte byte) {
            const auto character = std::to_integer<unsigned char>(byte);
            return character == 0U || character == '\r' || character == '\n';
        });
        FAIL_IF(invalid, ERR(CoreError, InvalidData),
                "Assistant authorization header contains an invalid byte");
        std::memcpy(_authorizationHeader.data(), value.data(), value.size());
        _authorizationHeader[value.size()] = '\0';
        return OK();
    }

    ReturnCode _sendFixed(std::string_view value, const char *label) {
        FAIL_IF(value.size() >= _outboundCapacity, ERR(CoreError, Overflow),
                "Assistant %s exceeds the outbound buffer", label);
        std::memcpy(_outbound, value.data(), value.size());
        _outbound[value.size()] = '\0';
        return _sendText(_outbound, value.size(), label);
    }

    ReturnCode _sendText(char *message, std::size_t bytes, const char *label) {
        FAIL_IF(!_connected, ERR(CoreError, InvalidState),
                "Cannot send assistant %s while disconnected", label);
        FAIL_IF(bytes >
                    static_cast<std::size_t>(std::numeric_limits<int>::max()),
                ERR(CoreError, Overflow),
                "Assistant %s exceeds the transport limit", label);
        const auto written = esp_transport_ws_send_raw(
            _webSocket, _opcodeWithFin(WS_TRANSPORT_OPCODES_TEXT), message,
            static_cast<int>(bytes), static_cast<int>(_config.writeTimeoutMs));
        FAIL_IF(written != static_cast<int>(bytes),
                ERR(CoreError, OperationFailed), "Failed to send assistant %s",
                label);
        return OK();
    }

    ReturnCode _beginFrame() {
        if (_isControl(_frameOpcode)) {
            FAIL_IF(!_frameFinal, ERR(CoreError, InvalidData),
                    "Fragmented assistant control frame");
            return OK();
        }
        if (_frameOpcode == WS_TRANSPORT_OPCODES_TEXT) {
            FAIL_IF(
                _messageStarted, ERR(CoreError, InvalidData),
                "Assistant started a text frame before finishing a message");
            _messageStarted = true;
            _messageSize = 0;
            return OK();
        }
        if (_frameOpcode == WS_TRANSPORT_OPCODES_CONT) {
            FAIL_IF(!_messageStarted, ERR(CoreError, InvalidData),
                    "Assistant sent an unexpected continuation frame");
            return OK();
        }
        FAIL(ERR(CoreError, InvalidData),
             "Assistant sent an unsupported WebSocket opcode");
    }

    ReturnCode _finishControlFrame(AssistantServerEvent &event,
                                   std::size_t payloadBytes) {
        if (_frameOpcode == WS_TRANSPORT_OPCODES_PING) {
            const auto written = esp_transport_ws_send_raw(
                _webSocket, _opcodeWithFin(WS_TRANSPORT_OPCODES_PONG),
                _readBuffer, static_cast<int>(payloadBytes),
                static_cast<int>(_config.writeTimeoutMs));
            FAIL_IF(written != static_cast<int>(payloadBytes),
                    ERR(CoreError, OperationFailed),
                    "Failed to answer assistant WebSocket ping");
            return OK();
        }
        if (_frameOpcode == WS_TRANSPORT_OPCODES_PONG) {
            return OK();
        }

        event.type = AssistantServerEventType::Closed;
        event.closeCode =
            payloadBytes >= 2U
                ? static_cast<uint16_t>(
                      (static_cast<uint8_t>(_readBuffer[0]) << 8U) |
                      static_cast<uint8_t>(_readBuffer[1]))
                : 1005U;
        _log_i("Assistant WebSocket close received: code=%u payload=%uB",
               static_cast<unsigned>(event.closeCode),
               static_cast<unsigned>(payloadBytes));
        const auto written = esp_transport_ws_send_raw(
            _webSocket, _opcodeWithFin(WS_TRANSPORT_OPCODES_CLOSE), _readBuffer,
            static_cast<int>(payloadBytes),
            static_cast<int>(_config.writeTimeoutMs));
        FAIL_IF(written != static_cast<int>(payloadBytes),
                ERR(CoreError, OperationFailed),
                "Failed to acknowledge assistant WebSocket close");
        // The close frame and its successful echo complete the WebSocket
        // handshake. Do not poll the raw socket here: on WSS, the peer's TLS
        // close-notify is readable encrypted data and ESP-IDF misclassifies it
        // as an unclean TCP close.
        _connected = false;
        return OK();
    }

    ReturnCode _parseMessage(AssistantServerEvent &event) {
        const char *parseEnd = nullptr;
        Json root{
            cJSON_ParseWithLengthOpts(_message, _messageSize, &parseEnd, false),
            &cJSON_Delete};
        FAIL_IF(!root || !cJSON_IsObject(root.get()),
                ERR(CoreError, InvalidData),
                "Assistant event is not a JSON object");
        FAIL_IF(parseEnd != _message + _messageSize,
                ERR(CoreError, InvalidData),
                "Assistant event has trailing JSON data");

        auto *type = cJSON_GetObjectItemCaseSensitive(root.get(), "type");
        FAIL_IF(!cJSON_IsString(type) || type->valuestring == nullptr,
                ERR(CoreError, InvalidData),
                "Assistant event has no string type");
        const std::string_view eventType{type->valuestring};
        if (eventType == "error") {
            event.type = AssistantServerEventType::Error;
            return OK();
        }
        if (eventType == "response.audio.started") {
            return _parseAudioStarted(root.get(), event);
        }
        if (eventType == "response.audio.delta") {
            return _parseAudioDelta(root.get(), event);
        }
        if (eventType == "response.audio.done") {
            event.type = AssistantServerEventType::AudioDone;
            return OK();
        }
        if (eventType == "response.done") {
            event.type = AssistantServerEventType::ResponseDone;
            return OK();
        }
        event.type = AssistantServerEventType::Unknown;
        return OK();
    }

    static ReturnCode _parseAudioStarted(const cJSON *root,
                                         AssistantServerEvent &event) {
        auto *format = cJSON_GetObjectItemCaseSensitive(root, "format");
        auto *sampleRate =
            cJSON_GetObjectItemCaseSensitive(root, "sample_rate");
        auto *sampleWidth =
            cJSON_GetObjectItemCaseSensitive(root, "sample_width");
        auto *channels = cJSON_GetObjectItemCaseSensitive(root, "channels");
        FAIL_IF(!cJSON_IsString(format) || format->valuestring == nullptr ||
                    std::string_view{format->valuestring} != "pcm16" ||
                    !cJSON_IsNumber(sampleRate) ||
                    !cJSON_IsNumber(sampleWidth) || !cJSON_IsNumber(channels),
                ERR(CoreError, InvalidData),
                "Assistant announced an invalid audio format");
        FAIL_IF(sampleRate->valuedouble != sampleRate->valueint ||
                    sampleWidth->valuedouble != sampleWidth->valueint ||
                    channels->valuedouble != channels->valueint ||
                    sampleRate->valueint <= 0 || sampleWidth->valueint <= 0 ||
                    channels->valueint <= 0 ||
                    sampleWidth->valueint > UINT8_MAX ||
                    channels->valueint > UINT8_MAX,
                ERR(CoreError, InvalidData),
                "Assistant audio format values are invalid");
        event.type = AssistantServerEventType::AudioStarted;
        event.sampleRate = static_cast<uint32_t>(sampleRate->valueint);
        event.sampleWidth = static_cast<uint8_t>(sampleWidth->valueint);
        event.channels = static_cast<uint8_t>(channels->valueint);
        return OK();
    }

    ReturnCode _parseAudioDelta(const cJSON *root,
                                AssistantServerEvent &event) {
        auto *audio = cJSON_GetObjectItemCaseSensitive(root, "audio");
        FAIL_IF(!cJSON_IsString(audio) || audio->valuestring == nullptr,
                ERR(CoreError, InvalidData),
                "Assistant audio delta has no base64 payload");
        const auto encodedBytes = std::strlen(audio->valuestring);
        std::size_t decodedBytes = 0;
        const auto decodeResult = mbedtls_base64_decode(
            reinterpret_cast<unsigned char *>(_decoded),
            _config.maximumDecodedAudioBytes, &decodedBytes,
            reinterpret_cast<const unsigned char *>(audio->valuestring),
            encodedBytes);
        FAIL_IF(decodeResult != 0 || decodedBytes == 0 ||
                    decodedBytes % sizeof(int16_t) != 0,
                ERR(CoreError, InvalidData),
                "Assistant audio delta has invalid base64 PCM");
        event.type = AssistantServerEventType::AudioDelta;
        event.audio = std::span<std::byte>{
            reinterpret_cast<std::byte *>(_decoded), decodedBytes};
        return OK();
    }

    void _resetReadState() {
        _frameOpcode = WS_TRANSPORT_OPCODES_NONE;
        _frameRemaining = 0;
        _frameFinal = false;
        _messageStarted = false;
        _messageSize = 0;
    }

    void _freeBuffers() {
        heap_caps_free(_outbound);
        heap_caps_free(_message);
        heap_caps_free(_decoded);
        heap_caps_free(_readBuffer);
        _outbound = nullptr;
        _message = nullptr;
        _decoded = nullptr;
        _readBuffer = nullptr;
        _outboundCapacity = 0;
    }

    AssistantWebSocketConfig _config{};
    std::array<char,
               Totem::AudioSink::webSocketMaxAuthorizationHeaderBytes + 1U>
        _authorizationHeader{};
    char *_outbound = nullptr;
    char *_message = nullptr;
    char *_decoded = nullptr;
    char *_readBuffer = nullptr;
    std::size_t _outboundCapacity = 0;
    std::size_t _messageSize = 0;
    std::size_t _frameRemaining = 0;
    esp_transport_handle_t _parent = nullptr;
    esp_transport_handle_t _webSocket = nullptr;
    ws_transport_opcodes_t _frameOpcode = WS_TRANSPORT_OPCODES_NONE;
    bool _frameFinal = false;
    bool _messageStarted = false;
    bool _connected = false;
    bool _active = false;

    static constexpr LogComponent logComponent = LogComponent::Audio;
};

} // namespace AiAudio
