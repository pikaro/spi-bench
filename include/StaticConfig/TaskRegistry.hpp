#pragma once

#include <cstdint>

struct TaskRegistryConfig {
    static constexpr uint8_t sourceCountMax = 10;
    static constexpr uint8_t sourceNameMaxLen = 16;
    static constexpr uint8_t observedTaskCountMax = 8;
    static constexpr uint32_t stopKillDelayMs = 1000;
};
