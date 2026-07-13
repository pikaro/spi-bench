#pragma once

#include "StaticConfig/Wifi.hpp"
#include "Wifi/Interfaces/Types.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Totem::Wifi {

namespace detail {

inline constexpr std::size_t maxSsidLength = 32;
inline constexpr std::size_t minPasswordLength = 8;
inline constexpr std::size_t maxPasswordLength = 63;
inline constexpr std::size_t maxPasswordSecretNameLength = 15;

[[nodiscard]] constexpr std::size_t boundedLength(const char *value,
                                                  std::size_t maxLength) {
    if (value == nullptr) {
        return maxLength + 1;
    }

    std::size_t length = 0;
    while (length <= maxLength && value[length] != '\0') {
        ++length;
    }
    return length;
}

[[nodiscard]] constexpr bool validSsid(const char *ssid) {
    const auto length = boundedLength(ssid, maxSsidLength);
    return length >= 1 && length <= maxSsidLength;
}

[[nodiscard]] constexpr bool validPasswordSecretName(const char *name) {
    const auto length = boundedLength(name, maxPasswordSecretNameLength);
    return length >= 1 && length <= maxPasswordSecretNameLength;
}

} // namespace detail

struct StationCredentials {
    const char *ssid = nullptr;
    const char *passwordSecretName = nullptr;
};

struct AccessPointCredentials {
    const char *ssid = nullptr;
    const char *passwordSecretName = nullptr;
};

struct StationConfig {
    StationCredentials credentials{};
    bool reconnect = Totem::StaticConfig::Wifi::defaultStationReconnect;
    uint8_t maxReconnectAttempts =
        Totem::StaticConfig::Wifi::defaultStationMaxReconnectAttempts;

    [[nodiscard]] constexpr bool validate() const {
        return detail::validSsid(credentials.ssid) &&
               detail::validPasswordSecretName(credentials.passwordSecretName);
    }
};

struct AccessPointConfig {
    AccessPointCredentials credentials{};
    uint8_t channel = Totem::StaticConfig::Wifi::defaultApChannel;
    bool hidden = false;
    uint8_t maxConnections = Totem::StaticConfig::Wifi::defaultApMaxConnections;

    [[nodiscard]] constexpr bool validate() const {
        return detail::validSsid(credentials.ssid) &&
               detail::validPasswordSecretName(
                   credentials.passwordSecretName) &&
               channel >= 1 && channel <= 13 && maxConnections > 0;
    }
};

struct Config {
    Mode mode = Mode::Disabled;
    std::optional<StationConfig> station = std::nullopt;
    std::optional<AccessPointConfig> accessPoint = std::nullopt;
    bool disableNvsStorage = true;

    [[nodiscard]] constexpr bool validate() const {
        switch (mode) {
        case Mode::Disabled:
            return true;
        case Mode::Station:
            if (!station.has_value()) {
                return false;
            }
            return station->validate();
        case Mode::AccessPoint:
            if (!accessPoint.has_value()) {
                return false;
            }
            return accessPoint->validate();
        default:
            return false;
        }
    }
};

} // namespace Totem::Wifi
