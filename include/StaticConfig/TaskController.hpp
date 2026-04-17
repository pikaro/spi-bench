#pragma once

#include <cstdint>

struct TaskControllerConfig {
    static constexpr uint8_t maxTaskNameLen = 16;
    static constexpr uint8_t maxTasksPerClass = 3;
};
