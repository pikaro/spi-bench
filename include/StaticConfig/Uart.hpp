#pragma once

#include <cstddef>
#include <cstdint>

struct UartConfig {
    static constexpr int baudRate = 115200;
    static constexpr uint8_t uartNumber = 0;
    static constexpr size_t maxReadLen = 128;
};
