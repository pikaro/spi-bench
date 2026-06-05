#pragma once

#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace Totem::LedDisplay::Animations::detail {

inline constexpr uint8_t simpleFullScale = std::numeric_limits<uint8_t>::max();
inline constexpr uint16_t simpleQ8Unit = 256U;

[[nodiscard]] inline constexpr uint32_t nonzero(uint32_t value,
                                                uint32_t fallback) {
    return value == 0 ? fallback : value;
}

[[nodiscard]] inline constexpr uint8_t atLeast(uint8_t value, uint8_t minimum) {
    return std::max<uint8_t>(value, minimum);
}

[[nodiscard]] inline constexpr uint16_t ringsQ8(uint32_t rings) {
    return static_cast<uint16_t>(rings * simpleQ8Unit);
}

[[nodiscard]] inline constexpr uint16_t radialQ8(uint8_t radial) {
    return static_cast<uint16_t>(radial) * simpleQ8Unit;
}

[[nodiscard]] inline constexpr uint16_t radius8ToStripQ8(uint8_t radius) {
    constexpr auto maxDistance =
        static_cast<uint32_t>((Config::ringCount - 1U) * simpleQ8Unit);
    return static_cast<uint16_t>((static_cast<uint32_t>(radius) * maxDistance) /
                                 simpleFullScale);
}

[[nodiscard]] inline constexpr uint8_t
stripRadius8(const Primitives::FieldPoint &point) {
    return static_cast<uint8_t>(point.stripRadius >> 8U);
}

[[nodiscard]] inline uint8_t progress8(uint32_t elapsedMs, uint32_t durationMs,
                                       uint8_t cycles = 1) {
    const auto resolvedDuration = nonzero(durationMs, 1U);
    const auto resolvedCycles = atLeast(cycles, 1);
    return static_cast<uint8_t>(
        (static_cast<uint64_t>(elapsedMs) * resolvedCycles * simpleQ8Unit) /
        resolvedDuration);
}

[[nodiscard]] inline uint16_t
progressQ8(uint32_t elapsedMs, uint32_t durationMs, uint8_t cycles = 1) {
    const auto resolvedDuration = nonzero(durationMs, 1U);
    const auto resolvedCycles = atLeast(cycles, 1);
    return static_cast<uint16_t>(
        (static_cast<uint64_t>(elapsedMs) * resolvedCycles * 65536ULL) /
        resolvedDuration);
}

[[nodiscard]] inline constexpr uint8_t pulseByDistance8(uint8_t distance,
                                                        uint8_t width) {
    if (width == 0) {
        return distance == 0 ? simpleFullScale : 0;
    }
    if (distance >= width) {
        return 0;
    }
    return Primitives::FieldMath::smoothstep8(static_cast<uint8_t>(
        simpleFullScale -
        ((static_cast<uint16_t>(distance) * simpleFullScale) / width)));
}

[[nodiscard]] inline constexpr uint8_t pulseByDistance16(uint16_t distance,
                                                         uint16_t width) {
    if (width == 0) {
        return distance == 0 ? simpleFullScale : 0;
    }
    if (distance >= width) {
        return 0;
    }
    return Primitives::FieldMath::smoothstep8(static_cast<uint8_t>(
        simpleFullScale -
        ((static_cast<uint32_t>(distance) * simpleFullScale) / width)));
}

[[nodiscard]] inline constexpr uint8_t highBand(uint8_t value, uint8_t width) {
    if (width == 0) {
        return value == simpleFullScale ? simpleFullScale : 0;
    }
    const auto threshold = static_cast<uint8_t>(
        simpleFullScale - std::min<uint8_t>(width, simpleFullScale));
    if (value <= threshold) {
        return 0;
    }
    return Primitives::FieldMath::smoothstep8(static_cast<uint8_t>(
        ((static_cast<uint16_t>(value - threshold) * simpleFullScale) /
         (simpleFullScale - threshold))));
}

[[nodiscard]] inline constexpr uint8_t directionalBehind(uint8_t center,
                                                         uint8_t theta) {
    return static_cast<uint8_t>(center - theta);
}

[[nodiscard]] inline constexpr uint8_t scale2(uint8_t value, uint8_t a,
                                              uint8_t b) {
    return Renderers::GenericRenderer::scale8(
        Renderers::GenericRenderer::scale8(value, a), b);
}

[[nodiscard]] inline constexpr uint8_t scale3(uint8_t value, uint8_t a,
                                              uint8_t b, uint8_t c) {
    return Renderers::GenericRenderer::scale8(scale2(value, a, b), c);
}

[[nodiscard]] inline constexpr uint8_t contrastAroundMid(uint8_t value,
                                                         uint8_t contrast) {
    const auto centered = static_cast<int16_t>(value) - 128;
    const auto scaled = static_cast<int16_t>(128) +
                        ((centered * static_cast<int16_t>(contrast)) / 128);
    if (scaled <= 0) {
        return 0;
    }
    if (scaled >= simpleFullScale) {
        return simpleFullScale;
    }
    return static_cast<uint8_t>(scaled);
}

[[nodiscard]] inline constexpr int16_t abs16(int16_t value) {
    return value < 0 ? static_cast<int16_t>(-value) : value;
}

[[nodiscard]] inline constexpr uint16_t approxDistance(int16_t dx, int16_t dy) {
    const auto ax = static_cast<uint16_t>(abs16(dx));
    const auto ay = static_cast<uint16_t>(abs16(dy));
    const auto hi = std::max<uint16_t>(ax, ay);
    const auto lo = std::min<uint16_t>(ax, ay);
    return static_cast<uint16_t>(
        std::min<uint32_t>(static_cast<uint32_t>(hi) + (lo / 2U), 65535U));
}

} // namespace Totem::LedDisplay::Animations::detail
