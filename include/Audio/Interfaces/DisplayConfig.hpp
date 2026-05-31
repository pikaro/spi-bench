#pragma once

#include "StaticConfig/Stacks.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include <cstdint>

namespace Totem::Audio {

struct FftDisplayConfig {
    bool showRawBands = false;
    uint8_t barGapPx = 2;
    uint8_t peakBarHoldMs = 120;
    uint8_t beatBarHoldMs = 140;
    uint8_t beatBarHeightPx = 3;
    Totem::TaskController::Config task{
        .name = "FftDisplay",
        .priority = 2,
        .stackSize = StaticConfig::TaskStacks::audioFftDisplay,
        .intervalMs = 100,
        .noCatchup = true,
    };

    [[nodiscard]] bool validate() const {
        return barGapPx % 2 == 0 && barGapPx >= 2 && barGapPx <= 8 &&
               beatBarHeightPx > 0 && beatBarHeightPx <= 6 && task.validate();
    }
};

} // namespace Totem::Audio
