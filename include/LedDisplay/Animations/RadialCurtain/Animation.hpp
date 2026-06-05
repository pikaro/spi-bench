#pragma once

#include "LedDisplay/Animations/RadialCurtain/Config.hpp"
#include "LedDisplay/Animations/detail/SimpleField.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct RadialCurtain {
    static constexpr AnimationKind kind = RadialCurtainSpec::kind;
    static constexpr Layer defaultLayer = RadialCurtainSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        RadialCurtainSpec::defaultLifetimeMs;
    static constexpr bool requiresFullFrame = false;
    static constexpr uint8_t minimumWidth = RadialCurtainSpec::minimumWidth;
    static constexpr uint8_t minimumSpeed = RadialCurtainSpec::minimumSpeed;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    RadialCurtainConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto duration =
            detail::nonzero(ctx.clock.durationMs, defaultLifetimeMs);
        const auto speed = std::max<uint8_t>(config.speed, minimumSpeed);
        const auto frontQ8 =
            static_cast<uint16_t>((static_cast<uint32_t>(detail::progressQ8(
                                       ctx.clock.elapsedMs, duration)) *
                                   speed) >>
                                  8U);
        const auto widthQ8 = static_cast<uint16_t>(std::max<uint8_t>(
                                 config.width, minimumWidth)) *
                             detail::simpleQ8Unit;
        const auto maxDistance = static_cast<uint16_t>(
            (Config::ringCount - 1U) * detail::simpleQ8Unit);
        const auto hue = static_cast<uint8_t>(config.hue + ctx.hueOffset);

        Primitives::forEachLogicalPixel(
            [&](const Primitives::FieldPoint &point) {
                const auto radialDistance =
                    config.outerOrigin
                        ? static_cast<uint16_t>(maxDistance -
                                                detail::radialQ8(point.radial))
                        : detail::radialQ8(point.radial);
                const auto spokeOffset = static_cast<uint16_t>(
                    (static_cast<uint16_t>(point.spoke) * config.spokePhase *
                     detail::simpleQ8Unit) /
                    Config::spokeCount);
                const auto tiltedFront = static_cast<uint16_t>(
                    frontQ8 + spokeOffset +
                    ((static_cast<uint16_t>(point.theta) * config.tilt) >> 2U));
                const auto distance = Primitives::FieldMath::absDiff16(
                    radialDistance, tiltedFront);
                const auto scale = detail::pulseByDistance16(distance, widthQ8);
                if (scale == 0) {
                    return;
                }
                const auto value =
                    Primitives::FieldMath::scale8(config.value, scale);
                if (value == 0) {
                    return;
                }
                ctx.canvas.pixel(point.spoke, point.radial,
                                 HsvColor{.hue = hue,
                                          .saturation = config.saturation,
                                          .value = value});
            });
    }
};

} // namespace Totem::LedDisplay::Animations
