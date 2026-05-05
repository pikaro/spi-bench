#pragma once

#include "Platform/Hardware.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::Wire::I2C {

inline constexpr size_t maxDevicesPerMaster = 32;

enum class BusId : uint8_t {
    Bus0,
    Bus1,
};

enum class AddressBits : uint8_t {
    Seven,
    Ten,
};

struct Pins {
    Pin sda;
    Pin scl;
};

struct DeviceHandle {
    static constexpr uint8_t invalidIndex = 0xFF;

    uint8_t index = invalidIndex;

    [[nodiscard]] constexpr bool valid() const {
        return index != invalidIndex;
    }
};

enum class PixelColor : uint8_t {
    Off,
    On,
    Invert,
};

} // namespace Totem::Wire::I2C
