#pragma once

#include "LedDisplay/Interfaces/OutputConfig.hpp"
#include "StaticConfig/LedDisplay.hpp"
#include "StaticConfig/Stacks.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include <cstdint>

namespace Totem::LedDisplay {

struct Config : LedDisplayConfig {
    uint8_t globalBrightness = 255;
    bool temporalDithering = true;
    // Zero disables; evaluated against the effective output brightness.
    uint8_t outputValueFloor = 0;
    uint8_t outputLumaFloor = 0;
    uint32_t frameBudgetUs = defaultFrameBudgetUs;
    Sk9822OutputConfig sk9822{};

    Totem::TaskController::Config task{
        .name = "LedDisplay",
        .priority = 3,
        .core = Totem::TaskController::Config::CorePreference::specific(1),
        .stackSize = Totem::StaticConfig::TaskStacks::ledDisplay,
        .intervalMs = taskIntervalMs,
        .noCatchup = true,
        .useNotify = true,
        .notifyExpectTimeout = true,
        .notifyTimeoutMs = taskIntervalMs,
    };

    [[nodiscard]] bool validate() const {
        return task.validate() && frameBudgetUs > 0 &&
               (!sk9822SpiOutput || sk9822.validate());
    }
};

} // namespace Totem::LedDisplay
