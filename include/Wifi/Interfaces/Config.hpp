#pragma once

#include "StaticConfig/Wifi.hpp"
#include "Wifi/Interfaces/Types.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::Wifi {

namespace detail {

inline constexpr std::size_t maxSsidLength = 32;
inline constexpr std::size_t maxPasswordLength = 63;

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

[[nodiscard]] constexpr bool validPassword(const char *password) {
    const auto length = boundedLength(password, maxPasswordLength);
    return length == 0 || (length >= 8 && length <= maxPasswordLength);
}

} // namespace detail

struct StationCredentials {
    const char *ssid = nullptr;
    const char *password = nullptr;
};

struct AccessPointCredentials {
    const char *ssid = nullptr;
    const char *password = nullptr;
};

struct StationConfig {
    StationCredentials credentials{};
    bool reconnect = Totem::StaticConfig::Wifi::defaultStationReconnect;
    uint8_t maxReconnectAttempts =
        Totem::StaticConfig::Wifi::defaultStationMaxReconnectAttempts;

    [[nodiscard]] constexpr bool validate() const {
        return detail::validSsid(credentials.ssid) &&
               detail::validPassword(credentials.password);
    }
};

struct AccessPointConfig {
    AccessPointCredentials credentials{};
    uint8_t channel = Totem::StaticConfig::Wifi::defaultApChannel;
    bool hidden = false;
    uint8_t maxConnections = Totem::StaticConfig::Wifi::defaultApMaxConnections;

    [[nodiscard]] constexpr bool validate() const {
        return detail::validSsid(credentials.ssid) &&
               detail::validPassword(credentials.password) && channel >= 1 &&
               channel <= 13 && maxConnections > 0;
    }
};

struct Config {
    Mode mode = Mode::Disabled;
    StationConfig station{};
    AccessPointConfig accessPoint{};
    bool disableNvsStorage = true;

    [[nodiscard]] constexpr bool validate() const {
        switch (mode) {
        case Mode::Disabled:
            return true;
        case Mode::Station:
            return station.validate();
        case Mode::AccessPoint:
            return accessPoint.validate();
        default:
            return false;
        }
    }

};

} // namespace Totem::Wifi
