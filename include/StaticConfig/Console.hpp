#pragma once

#include <cstddef>
#include <cstdint>

struct ConsoleConfig {
    static constexpr int baudRate = 921600;
    static constexpr uint8_t uartNumber = 0;
    static constexpr size_t txBufferSize = 2048;
    static constexpr size_t maxReadLen = 128;
};
