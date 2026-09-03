#pragma once

#include <cstdint>

enum class PeripheralButton : uint8_t {
    Bell,
    Calibration,
};

enum class PeripheralDial : uint8_t {
    Main,
};

enum class PeripheralMenu : uint8_t {
    Main,
};

enum class PeripheralLed : uint8_t {
    Bulb1,
    Bulb2,
    Onboard,
    PeakIndicator,
};
