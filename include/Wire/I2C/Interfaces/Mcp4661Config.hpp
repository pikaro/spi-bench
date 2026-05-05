#pragma once

#include "Wire/I2C/Interfaces/MasterConfig.hpp"
#include <cstdint>

namespace Totem::Wire::I2C {

inline constexpr uint16_t mcp4661MaxWiperValue = 0x01FF;

enum class Mcp4661Wiper : uint8_t {
    Wiper0,
    Wiper1,
};

enum class Mcp4661Register : uint8_t {
    Wiper0Volatile = 0x00,
    Wiper1Volatile = 0x01,
    Wiper0Eeprom = 0x02,
    Wiper1Eeprom = 0x03,
    Tcon = 0x04,
    Status = 0x05,
};

enum class Mcp4661StatusFlag : uint8_t {
    WriteProtect,
    Wiper0Lock,
    Wiper1Lock,
    EepromWriteActive,
};

enum class Mcp4661TconFlag : uint8_t {
    Wiper0ConnectB,
    Wiper0ConnectW,
    Wiper0ConnectA,
    Wiper0Shutdown,
    Wiper1ConnectB,
    Wiper1ConnectW,
    Wiper1ConnectA,
    Wiper1Shutdown,
    GeneralCallEnable,
};

struct Mcp4661Config {
    DeviceConfig device{
        .address = 0x28,
    };
    uint16_t initialWiper0 = 0;
    uint16_t initialWiper1 = 0;
    bool writeInitialState = true;
    bool writeInitialStateOnEnd = false;
    uint32_t commandSpacingMs = 10;

    [[nodiscard]] bool validate() const {
        return device.validate() && initialWiper0 <= mcp4661MaxWiperValue &&
               initialWiper1 <= mcp4661MaxWiperValue;
    }
};

} // namespace Totem::Wire::I2C
