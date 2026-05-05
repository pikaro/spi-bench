#pragma once

#include "Audio/Interfaces/Types.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace Totem::Audio {

struct BeatGroupConfig {
    BeatGroup group = BeatGroup::Bass;
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
    uint16_t minBpm = 60;
    uint16_t maxBpm = 180;

    [[nodiscard]] bool validate() const {
        return isBeatGroup(group) && energyBands.validate() &&
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
               ibiHistorySize >= 2 && ibiHistorySize <= 16 && minBpm > 0 &&
               maxBpm > minBpm;
    }
};

using BeatGroupConfigs = std::array<BeatGroupConfig, beatGroupCount>;

constexpr BeatGroupConfigs defaultBeatGroupConfigs() {
    return BeatGroupConfigs{{
        {
            .group = BeatGroup::Bass,
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
            .minBpm = 60,
            .maxBpm = 180,
        },
        {
            .group = BeatGroup::Mid,
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
            .minBpm = 60,
            .maxBpm = 220,
        },
        {
            .group = BeatGroup::High,
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
            .minBpm = 60,
            .maxBpm = 240,
        },
    }};
}

struct BeatTrackerConfig {
    BeatGroup primaryGroup = BeatGroup::Bass;
    BeatGroupConfigs groups = defaultBeatGroupConfigs();

    [[nodiscard]] bool validate() const {
        std::array<bool, beatGroupCount> seen{};
        for (const auto &group : groups) {
            if (!group.validate()) {
                return false;
            }
            const auto index = beatGroupIndex(group.group);
            if (index >= seen.size() || seen[index]) {
                return false;
            }
            seen[index] = true;
        }
        return isBeatGroup(primaryGroup);
    }
};

} // namespace Totem::Audio
