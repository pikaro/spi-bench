#pragma once

#include "LedTopology/Interfaces/Types.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::LedTopology::detail {

struct DenseUmbrella {
    static constexpr size_t stripCount = 2;
    static constexpr size_t segmentsPerStrip = 16;
    static constexpr size_t ledsPerSegment = 60;
    static constexpr size_t ledsPerStrip = segmentsPerStrip * ledsPerSegment;
    static constexpr size_t spokeCount = stripCount * segmentsPerStrip;
    static constexpr size_t ringCount = ledsPerSegment;
    static constexpr size_t totalPixelCount = stripCount * ledsPerStrip;

    // Nominal production geometry. The strip length is the 60-pixel cut length
    // at 144 LEDs/m, rounded to the nearest millimeter.
    static constexpr uint16_t ledsPerMeter = 144;
    static constexpr uint16_t innerRadiusMm = 60;
    static constexpr uint16_t centerGapDiameterMm = innerRadiusMm * 2U;
    static constexpr uint16_t radialStripLengthMm = static_cast<uint16_t>(
        ((ringCount * 1000U) + (ledsPerMeter / 2U)) / ledsPerMeter);
    static constexpr uint16_t outerRadiusMm =
        innerRadiusMm + radialStripLengthMm;

    [[nodiscard]] static constexpr PhysicalPixelIndex
    physicalFor(uint8_t spoke, uint8_t radial) {
        const auto physicalSpoke = static_cast<size_t>(spoke);
        const auto position = (physicalSpoke % 2U) == 0U
                                  ? static_cast<size_t>(radial)
                                  : ringCount - 1U - radial;
        return static_cast<PhysicalPixelIndex>((physicalSpoke * ringCount) +
                                               position);
    }

    [[nodiscard]] static constexpr uint8_t
    radialForPhysical(PhysicalPixelIndex pixel) {
        const auto physical = static_cast<size_t>(pixel);
        const auto spoke = physical / ringCount;
        const auto position = physical % ringCount;
        return static_cast<uint8_t>(
            (spoke % 2U) == 0U ? position : ringCount - 1U - position);
    }
};

} // namespace Totem::LedTopology::detail
