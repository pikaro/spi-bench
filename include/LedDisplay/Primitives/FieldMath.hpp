#pragma once

#include "LedDisplay/Interfaces/Color.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#include "LedDisplay/Renderers/Waveform.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace Totem::LedDisplay::Primitives::FieldMath {

inline constexpr uint8_t fullScale = std::numeric_limits<uint8_t>::max();

[[nodiscard]] inline constexpr uint8_t clampU8(uint16_t value) {
    return static_cast<uint8_t>(std::min<uint16_t>(value, fullScale));
}

[[nodiscard]] inline constexpr uint8_t absDiff8(uint8_t a, uint8_t b) {
    return a >= b ? static_cast<uint8_t>(a - b)
                  : static_cast<uint8_t>(b - a);
}

[[nodiscard]] inline constexpr uint16_t absDiff16(uint16_t a, uint16_t b) {
    return a >= b ? static_cast<uint16_t>(a - b)
                  : static_cast<uint16_t>(b - a);
}

[[nodiscard]] inline constexpr uint8_t scale8(uint8_t value, uint8_t scale) {
    return Renderers::GenericRenderer::scale8(value, scale);
}

[[nodiscard]] inline constexpr uint8_t qadd8(uint8_t a, uint8_t b) {
    return Renderers::GenericRenderer::qadd8(a, b);
}

[[nodiscard]] inline constexpr uint8_t average2(uint8_t a, uint8_t b) {
    return static_cast<uint8_t>((static_cast<uint16_t>(a) + b) / 2U);
}

[[nodiscard]] inline constexpr uint8_t average3(uint8_t a, uint8_t b,
                                                uint8_t c) {
    return static_cast<uint8_t>((static_cast<uint16_t>(a) + b + c) / 3U);
}

[[nodiscard]] inline constexpr uint8_t triangle8(uint8_t phase) {
    return (phase & 0x80U) == 0
               ? static_cast<uint8_t>(phase << 1U)
               : static_cast<uint8_t>((fullScale - phase) << 1U);
}

[[nodiscard]] inline constexpr uint8_t lerp8(uint8_t a, uint8_t b,
                                             uint8_t fraction) {
    const auto delta = static_cast<int16_t>(b) - static_cast<int16_t>(a);
    return static_cast<uint8_t>(
        static_cast<int16_t>(a) +
        ((delta * static_cast<int16_t>(fraction)) / fullScale));
}

[[nodiscard]] inline uint8_t sine8Q8(uint16_t phaseQ8) {
    const auto phase = static_cast<uint8_t>(phaseQ8 >> 8U);
    const auto fraction = static_cast<uint8_t>(phaseQ8 & 0xFFU);
    return lerp8(Renderers::Waveform::sine8(phase),
                 Renderers::Waveform::sine8(static_cast<uint8_t>(phase + 1U)),
                 fraction);
}

[[nodiscard]] inline constexpr uint8_t triangle8Q8(uint16_t phaseQ8) {
    const auto phase = static_cast<uint8_t>(phaseQ8 >> 8U);
    const auto fraction = static_cast<uint8_t>(phaseQ8 & 0xFFU);
    return lerp8(triangle8(phase), triangle8(static_cast<uint8_t>(phase + 1U)),
                 fraction);
}

[[nodiscard]] inline constexpr uint8_t smoothstep8(uint8_t value) {
    const auto x = static_cast<uint16_t>(value);
    const auto x2 = static_cast<uint16_t>((x * x) / fullScale);
    const auto shaped =
        (static_cast<uint32_t>(x2) * ((3U * fullScale) - (2U * x))) /
        fullScale;
    return clampU8(static_cast<uint16_t>(shaped));
}

[[nodiscard]] inline constexpr uint8_t angularDistance(uint8_t a, uint8_t b) {
    const auto diff = absDiff8(a, b);
    const auto wrapped = static_cast<uint16_t>(256U - diff);
    return static_cast<uint8_t>(
        std::min<uint16_t>(diff, wrapped));
}

[[nodiscard]] inline constexpr uint8_t ringPulse(uint8_t radius,
                                                 uint8_t center,
                                                 uint8_t width) {
    if (width == 0) {
        return radius == center ? fullScale : 0;
    }
    const auto distance = absDiff8(radius, center);
    if (distance >= width) {
        return 0;
    }
    return smoothstep8(static_cast<uint8_t>(
        fullScale - ((static_cast<uint16_t>(distance) * fullScale) / width)));
}

[[nodiscard]] inline constexpr uint8_t ringPulseQ8(uint16_t radiusQ8,
                                                   uint16_t centerQ8,
                                                   uint16_t widthQ8) {
    if (widthQ8 == 0) {
        return radiusQ8 == centerQ8 ? fullScale : 0;
    }
    const auto distance = absDiff16(radiusQ8, centerQ8);
    if (distance >= widthQ8) {
        return 0;
    }
    return smoothstep8(static_cast<uint8_t>(
        fullScale - ((static_cast<uint32_t>(distance) * fullScale) / widthQ8)));
}

[[nodiscard]] inline constexpr uint8_t foldedAngle(uint8_t theta,
                                                   uint8_t segments) {
    if (segments <= 1) {
        return theta;
    }
    const auto segmentWidth = static_cast<uint8_t>(
        std::max<uint16_t>(1U, static_cast<uint16_t>(256U / segments)));
    const auto inSegment = static_cast<uint8_t>(theta % segmentWidth);
    const auto halfWidth = static_cast<uint8_t>(
        std::max<uint16_t>(1U, static_cast<uint16_t>(segmentWidth / 2U)));
    const auto mirrored =
        inSegment <= halfWidth ? inSegment
                               : static_cast<uint8_t>(segmentWidth - inSegment);
    return static_cast<uint8_t>(
        (static_cast<uint16_t>(mirrored) * fullScale) / halfWidth);
}

[[nodiscard]] inline uint8_t standingWave(const FieldPoint &point,
                                          uint8_t radialMode,
                                          uint8_t angularMode,
                                          uint8_t phase) {
    const auto radialPhase = static_cast<uint8_t>(
        ((static_cast<uint32_t>(point.stripRadius >> 8U) * radialMode)) & 0xFFU);
    const auto angularPhase =
        static_cast<uint8_t>(point.theta * angularMode);
    return Renderers::Waveform::sine8(
        static_cast<uint8_t>(radialPhase + angularPhase + phase));
}

[[nodiscard]] inline HsvColor palette2(uint8_t hueA, uint8_t hueB,
                                       uint8_t saturation, uint8_t value,
                                       uint8_t mix) {
    const auto hue = static_cast<uint8_t>(
        hueA + Renderers::GenericRenderer::scale8(
                   static_cast<uint8_t>(hueB - hueA), mix));
    return HsvColor{.hue = hue, .saturation = saturation, .value = value};
}

[[nodiscard]] inline uint8_t applySensitivity(uint8_t value, uint8_t control,
                                              uint8_t sensitivity) {
    const auto boost = scale8(control, sensitivity);
    return qadd8(value, scale8(value, boost));
}

} // namespace Totem::LedDisplay::Primitives::FieldMath
