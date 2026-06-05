#pragma once

#include "LedDisplay/Animations/Shutter/Config.hpp"
#include "LedDisplay/Animations/detail/SimpleField.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct Shutter {
    static constexpr AnimationKind kind = ShutterSpec::kind;
    static constexpr Layer defaultLayer = ShutterSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        ShutterSpec::defaultLifetimeMs;
    static constexpr bool requiresFullFrame = false;
    static constexpr uint8_t minimumSegments = ShutterSpec::minimumSegments;
    static constexpr uint8_t minimumEdgeWidth = ShutterSpec::minimumEdgeWidth;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    ShutterConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto duration =
            detail::nonzero(ctx.clock.durationMs, defaultLifetimeMs);
        const auto progress = detail::progress8(ctx.clock.elapsedMs, duration,
                                                config.rotationCycles);
        const auto segments =
            std::max<uint8_t>(config.segments, minimumSegments);
        const auto width =
            std::max<uint8_t>(config.edgeWidth, minimumEdgeWidth);
        const auto openness = animatedOpenPct(ctx.clock.elapsedMs, duration);
        const auto hue = static_cast<uint8_t>(config.hue + ctx.hueOffset);

        Primitives::forEachLogicalPixel(
            [&](const Primitives::FieldPoint &point) {
                const auto folded = Primitives::FieldMath::foldedAngle(
                    static_cast<uint8_t>(point.theta + progress), segments);
                const auto edge = detail::pulseByDistance8(
                    Primitives::FieldMath::absDiff8(folded, openness), width);
                if (edge == 0) {
                    return;
                }
                const auto radialGlow = static_cast<uint8_t>(
                    192U +
                    (static_cast<uint16_t>(detail::stripRadius8(point)) / 4U));
                const auto value =
                    detail::scale2(config.value, edge, radialGlow);
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
    [[nodiscard]] uint8_t animatedOpenPct(uint32_t elapsedMs,
                                          uint32_t durationMs) const {
        if (config.mode == 0) {
            return config.openPct;
        }
        const auto sweep = Primitives::FieldMath::triangle8(
            detail::progress8(elapsedMs, durationMs));
        return Primitives::FieldMath::average2(config.openPct, sweep);
    }
};

} // namespace Totem::LedDisplay::Animations
