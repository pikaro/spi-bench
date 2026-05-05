#pragma once

#include "Wire/I2C/Interfaces/Types.hpp"
#include <cstdint>

namespace Totem::Wire::I2C {

struct MasterConfig {
    BusId busId = BusId::Bus0;
    Pins pins{};
    uint32_t clockHz = 400000;
    uint8_t glitchIgnoreCount = 7;
    bool enableInternalPullups = false;
    uint32_t transactionTimeoutMs = 50;

    [[nodiscard]] bool validate() const {
        return clockHz > 0 && transactionTimeoutMs > 0;
    }
};

struct DeviceConfig {
    uint16_t address = 0x3C;
    AddressBits addressBits = AddressBits::Seven;
    uint32_t clockHz = 0;
    uint32_t sclWaitUs = 0;
    bool disableAckCheck = false;

    [[nodiscard]] bool validate() const {
        if (addressBits == AddressBits::Seven) {
            return address <= 0x7F;
        }
        return address <= 0x3FF;
    }
};

} // namespace Totem::Wire::I2C
