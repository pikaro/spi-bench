#pragma once

#include <cstdint>

struct UartNodeConfig {
    static constexpr uint8_t rxQueueSize = 8;
    static constexpr uint8_t txQueueSize = 8;
};
