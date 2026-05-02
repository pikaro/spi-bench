#pragma once

#include "Audio/Interfaces/Types.hpp"
#include <cstdint>

namespace Totem::Audio {

struct BeatTrackerConfig {
    FftBandIndexRange energyBands{.lower = 0, .upper = 1};
    uint8_t minEnergy = 16;
    float sensitivity = 1.55F;
    float baselineAlpha = 0.04F;
    uint8_t onsetDelta = 8;
    uint32_t refractoryMs = 110;
    uint8_t ibiHistorySize = 8;
    uint16_t minBpm = 60;
    uint16_t maxBpm = 200;

    [[nodiscard]] bool validate() const {
        return energyBands.validate() && sensitivity >= 1.0F &&
               baselineAlpha > 0.0F && baselineAlpha <= 1.0F &&
               refractoryMs > 0 && ibiHistorySize >= 2 &&
               ibiHistorySize <= 16 && minBpm > 0 && maxBpm > minBpm;
    }
};

} // namespace Totem::Audio
