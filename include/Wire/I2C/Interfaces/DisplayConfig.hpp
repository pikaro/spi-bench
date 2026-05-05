#pragma once

#include "Wire/I2C/Interfaces/MasterConfig.hpp"
#include <cstdint>

namespace Totem::Wire::I2C {

struct Ssd1306Config {
    DeviceConfig device{};
    uint8_t width = 128;
    uint8_t height = 32;
    uint8_t contrast = 0x8F;
    bool flipHorizontal = false;
    bool flipVertical = false;
    bool clearOnBegin = true;
    bool turnOffOnEnd = true;

    [[nodiscard]] bool validate() const {
        return device.validate() && width == 128 &&
               (height == 32 || height == 64);
    }
};

} // namespace Totem::Wire::I2C
