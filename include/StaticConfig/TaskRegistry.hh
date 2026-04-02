#pragma once

#include <cstdint>

struct TaskRegistryConfig {
    static constexpr uint8_t controllerCountMax = 10;
    static constexpr uint8_t controllerNameMaxLen = 32;
    static constexpr uint32_t stopKillDelayMs = 1000;
};
