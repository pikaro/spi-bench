#pragma once

#include <cstddef>

struct ButtonConfig {
    static constexpr size_t maxButtons = 2;
    static constexpr size_t eventQueueSize = 8;
};
