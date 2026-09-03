#pragma once

#include "LedTopology/Interfaces/Types.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::LedTopology::detail {

struct Umbrella {
    static constexpr size_t stripCount = 4;
    static constexpr size_t segmentsPerStrip = 4;
    static constexpr size_t ledsPerSegment = 46;
    static constexpr size_t ledsPerStrip = segmentsPerStrip * ledsPerSegment;
    static constexpr size_t spokeCount = stripCount * segmentsPerStrip;
    static constexpr size_t ringCount = ledsPerSegment;
    static constexpr size_t totalPixelCount = stripCount * ledsPerStrip;

    // Approximate geometry of the legacy WS2812B installation.
    static constexpr uint16_t centerGapDiameterMm = 300;
    static constexpr uint16_t radialStripLengthMm = 300;
    static constexpr uint16_t innerRadiusMm = centerGapDiameterMm / 2U;
    static constexpr uint16_t outerRadiusMm =
        innerRadiusMm + radialStripLengthMm;

    [[nodiscard]] static constexpr PhysicalPixelIndex
    physicalFor(uint8_t spoke, uint8_t radial) {
        const auto strip = static_cast<size_t>(spoke / segmentsPerStrip);
        const auto logicalSegment =
            static_cast<size_t>(spoke % segmentsPerStrip);
        const auto physicalSegment = physicalSegmentFor(logicalSegment);
        const bool reversed = (physicalSegment % 2U) != 0U;
        const auto position =
            reversed ? (ledsPerSegment - 1U - radial) : radial;
        const auto physical = (strip * ledsPerStrip) +
                              (physicalSegment * ledsPerSegment) + position;
        return static_cast<PhysicalPixelIndex>(physical);
    }

    [[nodiscard]] static constexpr uint8_t
    radialForPhysical(PhysicalPixelIndex pixel) {
        const auto inStrip = static_cast<size_t>(pixel) % ledsPerStrip;
        const auto segment = inStrip / ledsPerSegment;
        const auto position = inStrip % ledsPerSegment;
        if ((segment % 2U) == 0U) {
            return static_cast<uint8_t>(position);
        }
        return static_cast<uint8_t>(ledsPerSegment - 1U - position);
    }

  private:
    [[nodiscard]] static constexpr size_t
    physicalSegmentFor(size_t logicalSegment) {
        return (segmentsPerStrip - 1U) - logicalSegment;
    }
};

} // namespace Totem::LedTopology::detail
