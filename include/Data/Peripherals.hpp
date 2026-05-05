#pragma once

#include <cstdint>

enum class PeripheralButton : uint8_t {
    Bell,
};

enum class PeripheralLed : uint8_t {
    Bulb1,
    Bulb2,
    Onboard,
    BeatIndicator,
};
