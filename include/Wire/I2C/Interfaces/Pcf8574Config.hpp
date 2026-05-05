#pragma once

#include "Wire/I2C/Interfaces/MasterConfig.hpp"
#include <cstdint>

namespace Totem::Wire::I2C {

enum class Pcf8574Pin : uint8_t {
    P0,
    P1,
    P2,
    P3,
    P4,
    P5,
    P6,
    P7,
};

enum class Pcf8574PinState : uint8_t {
    DrivenLow,
    Released,
};

struct Pcf8574Config {
    DeviceConfig device{
        .address = 0x20,
    };
    uint8_t initialReleasedMask = 0xFF;
    uint8_t shutdownReleasedMask = 0xFF;
    bool writeInitialState = true;
    bool writeShutdownState = true;

    [[nodiscard]] bool validate() const { return device.validate(); }
};

} // namespace Totem::Wire::I2C
