#pragma once

#include "LedDisplay/Animations/OrbitRing/Config.hpp"
#include "LedDisplay/Animations/detail/SimpleField.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct OrbitRing {
    static constexpr AnimationKind kind = OrbitRingSpec::kind;
    static constexpr Layer defaultLayer = OrbitRingSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        OrbitRingSpec::defaultLifetimeMs;
    static constexpr bool requiresFullFrame = false;
    static constexpr uint8_t minimumWidth = OrbitRingSpec::minimumWidth;
    static constexpr uint8_t minimumComets = OrbitRingSpec::minimumComets;
    static constexpr uint8_t minimumLaps = OrbitRingSpec::minimumLaps;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    OrbitRingConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto duration =
            detail::nonzero(ctx.clock.durationMs, defaultLifetimeMs);
        const auto progress =
            detail::progress8(ctx.clock.elapsedMs, duration, config.laps);
        const auto radialCenter = config.radius;
        const auto radialWidth =
            std::max<uint8_t>(config.radialWidth, minimumWidth);
        const auto angularWidth =
            std::max<uint8_t>(config.angularWidth, minimumWidth);
        const auto comets = std::max<uint8_t>(config.comets, minimumComets);
        const auto hue = static_cast<uint8_t>(config.hue + ctx.hueOffset);

        Primitives::forEachLogicalPixel(
            [&](const Primitives::FieldPoint &point) {
                const auto radialScale = Primitives::FieldMath::ringPulse(
                    detail::stripRadius8(point), radialCenter, radialWidth);
                if (radialScale == 0) {
                    return;
                }

                uint8_t angularScale = 0;
                for (uint8_t comet = 0; comet < comets; ++comet) {
                    const auto center = static_cast<uint8_t>(
                        progress +
                        ((static_cast<uint16_t>(comet) * detail::simpleQ8Unit) /
                         comets));
                    angularScale = std::max<uint8_t>(
                        angularScale, cometScale(point.theta, center,
                                                 angularWidth, config.trail));
                }
                if (angularScale == 0) {
                    return;
                }

                const auto value =
                    detail::scale2(config.value, radialScale, angularScale);
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
    [[nodiscard]] static constexpr uint8_t
    cometScale(uint8_t theta, uint8_t center, uint8_t width, uint8_t trail) {
        const auto head = detail::pulseByDistance8(
            Primitives::FieldMath::angularDistance(theta, center), width);
        if (trail == 0) {
            return head;
        }
        const auto behind = detail::directionalBehind(center, theta);
        const auto trailEnd = std::min<uint16_t>(
            detail::simpleFullScale, static_cast<uint16_t>(width) + trail);
        if (behind <= width || behind >= trailEnd) {
            return head;
        }
        const auto distanceIntoTrail = static_cast<uint8_t>(behind - width);
        const auto trailScale = Primitives::FieldMath::smoothstep8(
            static_cast<uint8_t>(detail::simpleFullScale -
                                 ((static_cast<uint16_t>(distanceIntoTrail) *
                                   detail::simpleFullScale) /
                                  trail)));
        return std::max<uint8_t>(head, trailScale);
    }
};

} // namespace Totem::LedDisplay::Animations
