#pragma once

#include "AudioFft/Interfaces/Types.hpp"
#include <cstdint>

namespace Totem::AudioFft {

struct TempoTrackerConfig {
    bool enabled = true;
    PeakGroup evidenceGroup = PeakGroup::Bass;
    uint16_t minBpm = 110;
    uint16_t maxBpm = 180;
    uint8_t intervalHistorySize = 8;
    uint8_t minConsistentIntervals = 3;
    uint8_t intervalTolerancePct = 12;
    uint16_t hitWindowMs = 120;
    uint8_t lockConfidence = 160;
    uint8_t confidenceGain = 18;
    uint8_t missPenalty = 50;
    uint8_t lostAfterMisses = 4;
    uint8_t stabilityWindowSize = 16;
    uint8_t reacquireStabilityScore = 96;
    uint8_t evidenceRateTolerancePct = 18;

    [[nodiscard]] bool validate() const {
        return isPeakGroup(evidenceGroup) && minBpm > 0 && maxBpm > minBpm &&
               intervalHistorySize >= 2 && intervalHistorySize <= 16 &&
               minConsistentIntervals >= 2 &&
               minConsistentIntervals <= intervalHistorySize &&
               intervalTolerancePct > 0 && intervalTolerancePct <= 50 &&
               hitWindowMs > 0 && lockConfidence > 0 &&
               confidenceGain > 0 && missPenalty > 0 && lostAfterMisses > 0 &&
               stabilityWindowSize >= 2 && stabilityWindowSize <= 16 &&
               evidenceRateTolerancePct > 0 && evidenceRateTolerancePct <= 50;
    }
};

} // namespace Totem::AudioFft
