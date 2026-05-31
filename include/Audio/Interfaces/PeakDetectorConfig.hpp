#pragma once

#include "Audio/Interfaces/Types.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace Totem::Audio {

struct PeakGroupConfig {
    PeakGroup group = PeakGroup::Bass;
    FftBandIndexRange energyBands{.lower = 0, .upper = 1};
    float minEnergy = 1.0F;
    float sensitivity = 1.35F;
    float baselineAlpha = 0.02F;
    float onsetDelta = 1.0F;
    float ambientEnergyMargin = 1.0F;
    float ambientSensitivity = 1.25F;
    float ambientAlpha = 0.01F;
    uint32_t refractoryMs = 320;
    uint8_t ibiHistorySize = 8;
    uint16_t minRatePerMinute = 60;
    uint16_t maxRatePerMinute = 180;

    [[nodiscard]] bool validate() const {
        return isPeakGroup(group) && energyBands.validate() &&
               std::isfinite(minEnergy) &&
               minEnergy >= 0.0F && std::isfinite(sensitivity) &&
               sensitivity >= 1.0F && std::isfinite(baselineAlpha) &&
               baselineAlpha > 0.0F && baselineAlpha <= 1.0F &&
               std::isfinite(onsetDelta) && onsetDelta > 0.0F &&
               std::isfinite(ambientEnergyMargin) &&
               ambientEnergyMargin >= 0.0F &&
               std::isfinite(ambientSensitivity) &&
               ambientSensitivity >= 1.0F &&
               std::isfinite(ambientAlpha) && ambientAlpha > 0.0F &&
               ambientAlpha <= 1.0F && refractoryMs > 0 &&
               ibiHistorySize >= 2 && ibiHistorySize <= 16 &&
               minRatePerMinute > 0 && maxRatePerMinute > minRatePerMinute;
    }
};

using PeakGroupConfigs = std::array<PeakGroupConfig, peakGroupCount>;

constexpr PeakGroupConfigs defaultPeakGroupConfigs() {
    return PeakGroupConfigs{{
        {
            .group = PeakGroup::Bass,
            .energyBands = {.lower = 0, .upper = 1},
            .minEnergy = 0.5F,
            .sensitivity = 1.18F,
            .baselineAlpha = 0.01F,
            .onsetDelta = 0.35F,
            .ambientEnergyMargin = 0.5F,
            .ambientSensitivity = 1.05F,
            .ambientAlpha = 0.006F,
            .refractoryMs = 320,
            .ibiHistorySize = 8,
            .minRatePerMinute = 60,
            .maxRatePerMinute = 180,
        },
        {
            .group = PeakGroup::Mid,
            .energyBands = {.lower = 2, .upper = 4},
            .minEnergy = 0.4F,
            .sensitivity = 1.22F,
            .baselineAlpha = 0.012F,
            .onsetDelta = 0.3F,
            .ambientEnergyMargin = 0.4F,
            .ambientSensitivity = 1.06F,
            .ambientAlpha = 0.008F,
            .refractoryMs = 240,
            .ibiHistorySize = 8,
            .minRatePerMinute = 60,
            .maxRatePerMinute = 220,
        },
        {
            .group = PeakGroup::High,
            .energyBands = {.lower = 5, .upper = 7},
            .minEnergy = 0.25F,
            .sensitivity = 1.25F,
            .baselineAlpha = 0.016F,
            .onsetDelta = 0.2F,
            .ambientEnergyMargin = 0.3F,
            .ambientSensitivity = 1.08F,
            .ambientAlpha = 0.01F,
            .refractoryMs = 180,
            .ibiHistorySize = 8,
            .minRatePerMinute = 60,
            .maxRatePerMinute = 240,
        },
    }};
}

struct PeakDetectorConfig {
    PeakGroup indicatorGroup = PeakGroup::Bass;
    PeakGroupConfigs groups = defaultPeakGroupConfigs();

    [[nodiscard]] bool validate() const {
        std::array<bool, peakGroupCount> seen{};
        for (const auto &group : groups) {
            if (!group.validate()) {
                return false;
            }
            const auto index = peakGroupIndex(group.group);
            if (index >= seen.size() || seen[index]) {
                return false;
            }
            seen[index] = true;
        }
        return isPeakGroup(indicatorGroup);
    }
};

} // namespace Totem::Audio
