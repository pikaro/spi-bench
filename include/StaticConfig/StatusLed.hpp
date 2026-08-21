#pragma once

#include <cstddef>
#include <cstdint>

struct StatusLedConfig {
    static constexpr std::size_t maxStates = 16;
    static constexpr uint32_t cycleIntervalMs = 500;

    static_assert(maxStates > 0 && maxStates <= 32,
                  "StatusLed active masks support 1..32 states");
    static_assert(maxStates >= 6,
                  "StatusLed needs six slots for predefined states");
};
