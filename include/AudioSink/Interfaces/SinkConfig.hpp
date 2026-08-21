#pragma once

#include "AudioSink/Interfaces/Types.hpp"
#include "AudioSource/Interfaces/SourceConfig.hpp"
#include "Network/Interfaces/Endpoint.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::AudioSink {

using I2SLinkConfig = AudioSource::I2SLinkConfig;

enum class AudioSinkKind : uint8_t {
    I2S,
    Tcp,
    WebSocket,
};

constexpr bool isAudioSinkKind(AudioSinkKind kind) {
    switch (kind) {
    case AudioSinkKind::I2S:
    case AudioSinkKind::Tcp:
    case AudioSinkKind::WebSocket:
        return true;
    default:
        return false;
    }
}

enum class I2SSinkDevicePreset : uint8_t {
    MAX98357,
    Custom,
};

constexpr bool isI2SSinkDevicePreset(I2SSinkDevicePreset preset) {
    switch (preset) {
    case I2SSinkDevicePreset::MAX98357:
    case I2SSinkDevicePreset::Custom:
        return true;
    default:
        return false;
    }
}

constexpr I2SLinkConfig max98357I2SLinkConfig() {
    return I2SLinkConfig{
        .audio =
            {
                .sampleRate = 44100,
                .channels = 2,
                .bitsPerSample = 16,
            },
        .hostClockRole = I2SHostClockRole::ProvidesClock,
        .format = I2SFormat::Philips,
        .channel = I2SChannelSelect::Stereo,
        .port = 0,
        .useApll = true,
    };
}

struct I2SSinkConfig {
    I2SSinkDevicePreset device = I2SSinkDevicePreset::MAX98357;
    I2SLinkConfig customLink{};
    I2SOutputPins pins;

    [[nodiscard]] constexpr I2SLinkConfig resolvedLink() const {
        switch (device) {
        case I2SSinkDevicePreset::MAX98357:
            return max98357I2SLinkConfig();
        case I2SSinkDevicePreset::Custom:
            return customLink;
        default:
            return {};
        }
    }

    [[nodiscard]] bool validate() const {
        return isI2SSinkDevicePreset(device) && resolvedLink().validate();
    }
};

struct NetworkSinkConfig {
    Network::Ipv4Endpoint endpoint{};
    const char *hostName = nullptr;
    AudioInfo audio{
        .sampleRate = 44100,
        .channels = 2,
        .bitsPerSample = 16,
    };
    uint16_t connectTimeoutMs = 3000;
    uint16_t writeTimeoutMs = 1000;
    uint16_t reconnectIntervalMs = 1000;

    [[nodiscard]] bool hasEndpoint() const {
        return endpoint.address != 0 && endpoint.port != 0;
    }

    [[nodiscard]] bool hasHostName() const {
        return hostName != nullptr && hostName[0] != '\0';
    }

    [[nodiscard]] bool validateEndpointOnly() const {
        return audio.validate() && hasEndpoint() && connectTimeoutMs > 0 &&
               writeTimeoutMs > 0 && reconnectIntervalMs > 0;
    }

    [[nodiscard]] bool validateEndpointOrHostName() const {
        return audio.validate() && endpoint.port != 0 &&
               (hasEndpoint() || hasHostName()) && connectTimeoutMs > 0 &&
               writeTimeoutMs > 0 && reconnectIntervalMs > 0;
    }
};

struct TcpSinkConfig {
    NetworkSinkConfig network{};

    [[nodiscard]] bool validate() const {
        return network.validateEndpointOnly();
    }
};

struct WebSocketSinkConfig {
    NetworkSinkConfig network{};
    const char *path = "/";
    bool secure = true;
    const char *authorizationHeaderSecretName = nullptr;
    const char *trustedRootPem = nullptr;
    std::size_t packetBytes = webSocketDefaultPacketBytes;

    [[nodiscard]] bool hasAuthorizationHeader() const {
        return authorizationHeaderSecretName != nullptr &&
               authorizationHeaderSecretName[0] != '\0';
    }

    [[nodiscard]] bool hasValidAuthorizationSecretName() const {
        if (!hasAuthorizationHeader()) {
            return true;
        }
        std::size_t length = 0;
        while (length <= webSocketMaxAuthorizationSecretNameBytes &&
               authorizationHeaderSecretName[length] != '\0') {
            ++length;
        }
        return length <= webSocketMaxAuthorizationSecretNameBytes;
    }

    [[nodiscard]] bool hasTrustedRootPem() const {
        return trustedRootPem != nullptr && trustedRootPem[0] != '\0';
    }

    [[nodiscard]] bool validate() const {
        return network.validateEndpointOrHostName() && path != nullptr &&
               path[0] == '/' && packetBytes > 0 &&
               packetBytes <= webSocketMaxPacketBytes &&
               hasValidAuthorizationSecretName() &&
               (!secure || hasTrustedRootPem());
    }
};

} // namespace Totem::AudioSink
