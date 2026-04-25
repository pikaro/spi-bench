#pragma once

#include "StaticConfig/TaskController.hpp"
#include <cstdint>

struct TaskRegistryConfig {
    static constexpr uint8_t sourceCountMax = 10;
    static constexpr uint8_t sourceNameMaxLen = 16;
    static constexpr uint8_t externalTaskCountMax = 8;
    static constexpr uint8_t managedTaskCountMax =
        sourceCountMax * TaskControllerConfig::maxTasksPerClass;
    static constexpr uint8_t observedTaskCountMax =
        managedTaskCountMax + externalTaskCountMax;
    static constexpr uint32_t stopKillDelayMs = 1000;
};
