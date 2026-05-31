#pragma once

#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::LedDisplay::Primitives {

struct FieldPoint {
    uint8_t spoke = 0;
    uint8_t radial = 0;
    uint8_t theta = 0;
    uint16_t stripRadius = 0;
    uint16_t physicalRadius = 0;
    int16_t x = 0;
    int16_t y = 0;
};

namespace detail {

inline constexpr int16_t centeredSineQ15(uint8_t phase) {
    return static_cast<int16_t>(
        (static_cast<int16_t>(Renderers::GenericRenderer::sine8(phase)) -
         128) *
        256);
}

inline constexpr int16_t scaleQ15(int16_t value, uint16_t scale) {
    return static_cast<int16_t>(
        (static_cast<int32_t>(value) * static_cast<int32_t>(scale)) /
        65535);
}

} // namespace detail

[[nodiscard]] inline constexpr uint8_t thetaForSpoke(uint8_t spoke) {
    return static_cast<uint8_t>(
        (static_cast<uint32_t>(spoke) * 256U) / Config::spokeCount);
}

[[nodiscard]] inline constexpr uint16_t stripRadiusForRadial(uint8_t radial) {
    return static_cast<uint16_t>(
        (((static_cast<uint32_t>(radial) * 2U) + 1U) * 65535U) /
        (Config::ringCount * 2U));
}

[[nodiscard]] inline constexpr uint16_t
physicalRadiusForRadial(uint8_t radial) {
    const auto radialCenterMm =
        static_cast<uint32_t>(Config::innerRadiusMm) +
        ((((static_cast<uint32_t>(radial) * 2U) + 1U) *
          Config::radialStripLengthMm) /
         (Config::ringCount * 2U));
    return static_cast<uint16_t>(
        (radialCenterMm * 65535U) / Config::outerRadiusMm);
}

[[nodiscard]] inline constexpr FieldPoint fieldPoint(uint8_t spoke,
                                                     uint8_t radial) {
    const auto theta = thetaForSpoke(spoke);
    const auto radius = physicalRadiusForRadial(radial);
    return FieldPoint{
        .spoke = spoke,
        .radial = radial,
        .theta = theta,
        .stripRadius = stripRadiusForRadial(radial),
        .physicalRadius = radius,
        .x = detail::scaleQ15(detail::centeredSineQ15(theta + 64U), radius),
        .y = detail::scaleQ15(detail::centeredSineQ15(theta), radius),
    };
}

template <typename Callback> inline void forEachLogicalPixel(Callback &&cb) {
    for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            cb(fieldPoint(spoke, radial));
        }
    }
}

} // namespace Totem::LedDisplay::Primitives
