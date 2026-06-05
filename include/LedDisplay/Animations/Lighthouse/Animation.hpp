#pragma once

#include "LedDisplay/Animations/Lighthouse/Config.hpp"
#include "LedDisplay/Animations/detail/SimpleField.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct Lighthouse {
    static constexpr AnimationKind kind = LighthouseSpec::kind;
    static constexpr Layer defaultLayer = LighthouseSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        LighthouseSpec::defaultLifetimeMs;
    static constexpr bool requiresFullFrame = false;
    static constexpr uint8_t minimumBeamWidth =
        LighthouseSpec::minimumBeamWidth;
    static constexpr uint8_t minimumCycles = LighthouseSpec::minimumCycles;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    LighthouseConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto duration =
            detail::nonzero(ctx.clock.durationMs, defaultLifetimeMs);
        const auto center =
            detail::progress8(ctx.clock.elapsedMs, duration, config.cycles);
        const auto beamWidth = angleWidth(config.beamWidth);
        const auto trailWidth = angleWidth(config.trailSpokes);
        const auto hue = static_cast<uint8_t>(config.hue + ctx.hueOffset);
        const auto outer = config.outerRing == 0
                               ? static_cast<uint8_t>(Config::ringCount - 1U)
                               : std::min<uint8_t>(config.outerRing,
                                                   static_cast<uint8_t>(
                                                       Config::ringCount - 1U));
        const auto inner = std::min<uint8_t>(config.innerRing, outer);

        Primitives::forEachLogicalPixel(
            [&](const Primitives::FieldPoint &point) {
                if (point.radial < inner || point.radial > outer) {
                    return;
                }
                const auto angular =
                    beamScale(point.theta, center, beamWidth, trailWidth);
                if (angular == 0) {
                    return;
                }
                const auto radialScale = static_cast<uint8_t>(
                    128U +
                    (static_cast<uint16_t>(detail::stripRadius8(point)) / 2U));
                const auto value =
                    detail::scale2(config.value, angular, radialScale);
                if (value == 0) {
                    return;
                }
                ctx.canvas.pixel(point.spoke, point.radial,
                                 HsvColor{.hue = hue,
                                          .saturation = config.saturation,
                                          .value = value});
            });
    }

  private:
    [[nodiscard]] static constexpr uint8_t angleWidth(uint8_t spokes) {
        const auto resolvedSpokes = std::max<uint8_t>(spokes, minimumBeamWidth);
        return static_cast<uint8_t>(std::max<uint16_t>(
            1U, (static_cast<uint16_t>(resolvedSpokes) * detail::simpleQ8Unit) /
                    Config::spokeCount));
    }

    [[nodiscard]] static constexpr uint8_t beamScale(uint8_t theta,
                                                     uint8_t center,
                                                     uint8_t beamWidth,
                                                     uint8_t trailWidth) {
        const auto head = detail::pulseByDistance8(
            Primitives::FieldMath::angularDistance(theta, center), beamWidth);
        if (trailWidth == 0) {
            return head;
        }
        const auto behind = detail::directionalBehind(center, theta);
        const auto trailEnd =
            std::min<uint16_t>(detail::simpleFullScale,
                               static_cast<uint16_t>(beamWidth) + trailWidth);
        if (behind <= beamWidth || behind >= trailEnd) {
            return head;
        }
        const auto distanceIntoTrail = static_cast<uint8_t>(behind - beamWidth);
        const auto trailScale = static_cast<uint8_t>(
            (static_cast<uint16_t>(trailWidth - distanceIntoTrail) *
             detail::simpleFullScale) /
            trailWidth);
        return std::max<uint8_t>(
            head, Primitives::FieldMath::smoothstep8(trailScale));
    }
};

} // namespace Totem::LedDisplay::Animations
