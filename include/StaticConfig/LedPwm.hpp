#pragma once

#include <cstddef>

struct LedPwmConfig {
    static constexpr size_t commandQueueSize = 5;
    static constexpr size_t maxLeds = 3;
    static constexpr size_t animationSlots = 10;
};
