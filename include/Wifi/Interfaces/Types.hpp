#pragma once

#include <array>
#include <cstdint>

namespace Totem::Wifi {

enum class Mode : uint8_t {
    Disabled,
    AccessPoint,
    Station,
};

struct Ipv4Address {
    uint32_t raw = 0;
    bool valid = false;

    [[nodiscard]] constexpr std::array<uint8_t, 4> octets() const {
        return {
            static_cast<uint8_t>(raw & 0xFFU),
            static_cast<uint8_t>((raw >> 8U) & 0xFFU),
            static_cast<uint8_t>((raw >> 16U) & 0xFFU),
            static_cast<uint8_t>((raw >> 24U) & 0xFFU),
        };
    }
};

struct Status {
    Mode mode = Mode::Disabled;
    bool started = false;
    bool accessPointStarted = false;
    uint8_t accessPointStationCount = 0;
    bool stationConnected = false;
    Ipv4Address stationIpv4{};
};

} // namespace Totem::Wifi
