#pragma once

#include "LedDisplay/Animations/Vortex/Config.hpp"
#include "LedDisplay/Animations/detail/SimpleField.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct Vortex {
    static constexpr AnimationKind kind = VortexSpec::kind;
    static constexpr Layer defaultLayer = VortexSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs = VortexSpec::defaultLifetimeMs;
    static constexpr bool requiresFullFrame = false;
    static constexpr uint8_t minimumArms = VortexSpec::minimumArms;
    static constexpr uint8_t minimumWidth = VortexSpec::minimumWidth;
    static constexpr uint8_t minimumCycles = VortexSpec::minimumCycles;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    VortexConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto duration =
            detail::nonzero(ctx.clock.durationMs, defaultLifetimeMs);
        const auto progress =
            detail::progressQ8(ctx.clock.elapsedMs, duration, config.cycles);
        const auto arms = std::max<uint8_t>(config.arms, minimumArms);
        const auto width = std::max<uint8_t>(config.width, minimumWidth);
        const auto baseHue = static_cast<uint8_t>(config.hue + ctx.hueOffset);

        Primitives::forEachLogicalPixel(
            [&](const Primitives::FieldPoint &point) {
                const auto phase = static_cast<uint16_t>(
                    (static_cast<uint32_t>(point.theta) * arms *
                     detail::simpleQ8Unit) +
                    (static_cast<uint32_t>(point.stripRadius) * config.twist) +
                    progress);
                const auto band = detail::highBand(
                    Primitives::FieldMath::sine8Q8(phase), width);
                if (band == 0) {
                    return;
                }

                const auto value =
                    Primitives::FieldMath::scale8(config.value, band);
                if (value == 0) {
                    return;
                }
                const auto hue = static_cast<uint8_t>(
                    baseHue + Primitives::FieldMath::scale8(
                                  detail::stripRadius8(point), config.hueStep));
                ctx.canvas.pixel(point.spoke, point.radial,
                                 HsvColor{.hue = hue,
                                          .saturation = config.saturation,
                                          .value = value});
            });
    }
};

} // namespace Totem::LedDisplay::Animations
