#pragma once

#include "Wifi/Facade.hpp"

namespace MasterWifiCredentials {

struct Station {
    inline static constexpr const char *ssid = "station-ssid";
    inline static constexpr const char *password = "station-password";
};

struct AccessPoint {
    inline static constexpr const char *ssid = "device-ap-ssid";
    inline static constexpr const char *password = "device-ap-password";
};

inline constexpr Totem::Wifi::Mode mode = Totem::Wifi::Mode::Disabled;

} // namespace MasterWifiCredentials
