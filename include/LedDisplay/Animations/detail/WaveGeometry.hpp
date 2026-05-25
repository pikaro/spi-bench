#pragma once

#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#include "LedDisplay/Renderers/Waveform.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace Totem::LedDisplay::Animations::detail {

static constexpr uint16_t q8Unit = 256;
static constexpr uint8_t fullScale = std::numeric_limits<uint8_t>::max();

[[nodiscard]] constexpr uint8_t phase8(uint32_t elapsedMs, uint16_t periodMs) {
    const auto elapsedInPeriod = elapsedMs % periodMs;
    return static_cast<uint8_t>((elapsedInPeriod * 256UL) / periodMs);
}

[[nodiscard]] constexpr uint8_t resolvedTravelRings(uint8_t travelRings) {
    if (travelRings == 0) {
        return Config::ringCount;
    }
    return std::clamp<uint8_t>(travelRings, 1, Config::ringCount);
}

[[nodiscard]] constexpr uint16_t maxTravelDistanceQ8(uint8_t travelRings) {
    const auto resolved = resolvedTravelRings(travelRings);
    return static_cast<uint16_t>(resolved - 1U) * q8Unit;
}

[[nodiscard]] constexpr uint16_t originRadialQ8(uint16_t distanceFromOriginQ8,
                                                bool outerOrigin) {
    constexpr auto lastRadialQ8 =
        static_cast<uint16_t>((Config::ringCount - 1U) * q8Unit);
    return outerOrigin
               ? static_cast<uint16_t>(lastRadialQ8 - distanceFromOriginQ8)
               : distanceFromOriginQ8;
}

[[nodiscard]] constexpr uint16_t originDistanceQ8(uint16_t radialQ8,
                                                  bool outerOrigin) {
    constexpr auto lastRadialQ8 =
        static_cast<uint16_t>((Config::ringCount - 1U) * q8Unit);
    return outerOrigin ? static_cast<uint16_t>(lastRadialQ8 - radialQ8)
                       : radialQ8;
}

[[nodiscard]] constexpr uint16_t distanceQ8(uint16_t a, uint16_t b) {
    return a > b ? static_cast<uint16_t>(a - b) : static_cast<uint16_t>(b - a);
}

[[nodiscard]] inline uint8_t lobeScale(uint16_t distance, uint16_t radius) {
    if (distance >= radius) {
        return 0;
    }
    const auto remaining = static_cast<uint32_t>(radius - distance);
    const auto phase = static_cast<uint8_t>((remaining * 64U) / radius);
    if (phase == 0) {
        return 0;
    }
    const auto wave = Renderers::Waveform::sine8(phase);
    if (wave <= 128) {
        return 0;
    }
    return static_cast<uint8_t>(
        std::min<uint16_t>(((wave - 128U) * 2U) + 1U, fullScale));
}

[[nodiscard]] inline uint8_t applyPercent(uint8_t value, uint16_t percent) {
    return static_cast<uint8_t>(std::min<uint32_t>(
        (static_cast<uint32_t>(value) * percent) / 100U, fullScale));
}

[[nodiscard]] inline uint16_t
spokeGainPercent(uint8_t spoke, uint16_t maxPercent, uint8_t phaseStep) {
    if (maxPercent <= 100U || phaseStep == 0) {
        return 100U;
    }
    const auto phase =
        static_cast<uint8_t>(192U + (static_cast<uint16_t>(spoke) * phaseStep));
    const auto wave = Renderers::Waveform::sine8(phase);
    return static_cast<uint16_t>(
        100U +
        (((static_cast<uint32_t>(maxPercent - 100U) * wave) / fullScale)));
}

[[nodiscard]] inline uint8_t applySpokeGain(uint8_t value, uint8_t spoke,
                                            uint16_t maxPercent,
                                            uint8_t phaseStep) {
    return applyPercent(value, spokeGainPercent(spoke, maxPercent, phaseStep));
}

[[nodiscard]] inline uint8_t repeatedAttenuation(uint32_t count,
                                                 uint8_t attenuation) {
    if (attenuation == fullScale || count == 0) {
        return fullScale;
    }

    auto result = fullScale;
    auto factor = attenuation;
    while (count > 0 && result > 0) {
        if ((count & 1U) != 0) {
            result = Renderers::GenericRenderer::scale8(result, factor);
        }
        count >>= 1U;
        if (count != 0) {
            factor = Renderers::GenericRenderer::scale8(factor, factor);
        }
    }
    return result;
}

[[nodiscard]] inline uint8_t blendValue(uint8_t from, uint8_t to,
                                        uint8_t scale) {
    if (to >= from) {
        return static_cast<uint8_t>(
            from + ((static_cast<uint16_t>(to - from) * scale) / fullScale));
    }
    return static_cast<uint8_t>(
        from - ((static_cast<uint16_t>(from - to) * scale) / fullScale));
}

} // namespace Totem::LedDisplay::Animations::detail
