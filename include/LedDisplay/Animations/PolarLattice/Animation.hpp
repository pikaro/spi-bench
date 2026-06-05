#pragma once

#include "LedDisplay/Animations/PolarLattice/Config.hpp"
#include "LedDisplay/Animations/detail/SimpleField.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct PolarLattice {
    static constexpr AnimationKind kind = PolarLatticeSpec::kind;
    static constexpr Layer defaultLayer = PolarLatticeSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        PolarLatticeSpec::defaultLifetimeMs;
    static constexpr bool requiresFullFrame = false;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    PolarLatticeConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto duration =
            detail::nonzero(ctx.clock.durationMs, defaultLifetimeMs);
        const auto progress =
            static_cast<uint16_t>((static_cast<uint32_t>(detail::progressQ8(
                                       ctx.clock.elapsedMs, duration)) *
                                   config.speed) >>
                                  8U);
        const auto hue = static_cast<uint8_t>(config.hue + ctx.hueOffset);

        Primitives::forEachLogicalPixel(
            [&](const Primitives::FieldPoint &point) {
                const auto radial =
                    Primitives::FieldMath::sine8Q8(static_cast<uint16_t>(
                        (static_cast<uint32_t>(point.stripRadius) *
                         config.radialMode) +
                        progress));
                const auto angular =
                    Primitives::FieldMath::sine8Q8(static_cast<uint16_t>(
                        (static_cast<uint32_t>(point.theta) *
                         config.angularMode * detail::simpleQ8Unit)));
                const auto mixed =
                    Primitives::FieldMath::lerp8(radial, angular, config.mix);
                const auto contrasted =
                    detail::contrastAroundMid(mixed, config.contrast);
                const auto band = detail::highBand(contrasted, 128);
                if (band == 0) {
                    return;
                }
                const auto value =
                    Primitives::FieldMath::scale8(config.value, band);
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
