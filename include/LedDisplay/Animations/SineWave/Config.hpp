#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG SineWaveConfig {
    uint8_t hue = 96;
    uint8_t saturation = 192;
    uint8_t value = 120;
    uint8_t baseValue = 0;
    uint8_t width = 1;
    uint16_t durationMs = 3200;
    uint8_t wavelength = 8;
    bool outerOrigin = false;
    uint8_t travelRings = 0;
    uint16_t spokeGainPct = 100;
    uint8_t tailDecay = 8;
    uint8_t peakHold = 160;
};

struct SineWaveSpec {
    static constexpr AnimationKind kind = AnimationKind::SineWave;
    static constexpr Layer defaultLayer = Layer::Effect;
    static constexpr uint16_t defaultDurationMs = 3200;
    static constexpr uint16_t minimumDurationMs = 100;
    static constexpr uint8_t minimumWidth = 1;
    static constexpr uint8_t minimumWavelength = 2;
    static constexpr uint8_t spokeGainPhaseStep = 64;
    static constexpr uint8_t defaultTailDecay = 8;
    static constexpr uint8_t defaultPeakHold = 160;
    static constexpr uint8_t valueFloor = 10;

    [[nodiscard]] static constexpr uint16_t
    projectedLifetimeMs(const SineWaveConfig &config = {}) {
        const auto duration =
            std::max<uint16_t>(config.durationMs, minimumDurationMs);
        const auto maxDistanceQ8 = travelDistanceQ8(config.travelRings);
        if (maxDistanceQ8 == 0) {
            return duration;
        }

        const auto peakValue = std::max(config.value, config.baseValue);
        if (peakValue <= valueFloor) {
            return duration;
        }

        const auto peakDecayHold = static_cast<uint16_t>(
            std::numeric_limits<uint8_t>::max() - config.peakHold);
        const auto decayDenominator =
            static_cast<uint32_t>(config.tailDecay) * peakDecayHold;
        if (decayDenominator == 0) {
            return 0;
        }

        const auto loss = static_cast<uint32_t>(peakValue - valueFloor);
        constexpr auto fullScale =
            static_cast<uint32_t>(std::numeric_limits<uint8_t>::max());
        constexpr auto q8Unit = static_cast<uint32_t>(256);
        const auto ageQ8 = ceilDiv(loss * q8Unit * fullScale, decayDenominator);
        const auto tailMs =
            ceilDiv(static_cast<uint64_t>(ageQ8) * duration, maxDistanceQ8);
        const auto total = static_cast<uint64_t>(duration) + tailMs;
        if (total > std::numeric_limits<uint16_t>::max()) {
            return 0;
        }
        return static_cast<uint16_t>(total);
    }

  private:
    [[nodiscard]] static constexpr uint32_t ceilDiv(uint64_t numerator,
                                                    uint64_t denominator) {
        return static_cast<uint32_t>((numerator + denominator - 1U) /
                                     denominator);
    }

    [[nodiscard]] static constexpr uint8_t
    resolvedTravelRings(uint8_t travelRings) {
        if (travelRings == 0) {
            return static_cast<uint8_t>(Config::ringCount);
        }
        return std::clamp<uint8_t>(travelRings, 1,
                                   static_cast<uint8_t>(Config::ringCount));
    }

    [[nodiscard]] static constexpr uint16_t
    travelDistanceQ8(uint8_t travelRings) {
        return static_cast<uint16_t>(resolvedTravelRings(travelRings) - 1U) *
               256U;
    }
};

static_assert(std::is_trivially_copyable_v<SineWaveConfig>,
              "SineWaveConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
