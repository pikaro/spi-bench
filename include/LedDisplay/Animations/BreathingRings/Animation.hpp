#pragma once

#include "LedDisplay/Animations/BreathingRings/Config.hpp"
#include "LedDisplay/Animations/detail/SimpleField.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct BreathingRings {
    static constexpr AnimationKind kind = BreathingRingsSpec::kind;
    static constexpr Layer defaultLayer = BreathingRingsSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        BreathingRingsSpec::defaultLifetimeMs;
    static constexpr bool requiresFullFrame = false;
    static constexpr uint8_t minimumSpacing =
        BreathingRingsSpec::minimumSpacing;
    static constexpr uint8_t minimumWidth = BreathingRingsSpec::minimumWidth;
    static constexpr uint8_t minimumCycles = BreathingRingsSpec::minimumCycles;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    BreathingRingsConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto duration =
            detail::nonzero(ctx.clock.durationMs, defaultLifetimeMs);
        const auto spacing = std::max<uint8_t>(config.spacing, minimumSpacing);
        const auto width = std::max<uint8_t>(config.width, minimumWidth);
        const auto progress =
            detail::progress8(ctx.clock.elapsedMs, duration, config.cycles);
        const auto phaseOffset = directionPhase(progress);
        const auto bandWidth = static_cast<uint8_t>(std::max<uint16_t>(
            1U, (static_cast<uint16_t>(width) * detail::simpleFullScale) /
                    spacing));
        const auto hue = static_cast<uint8_t>(config.hue + ctx.hueOffset);

        Primitives::forEachLogicalPixel(
            [&](const Primitives::FieldPoint &point) {
                const auto ringPhase = static_cast<uint8_t>(
                    ((static_cast<uint16_t>(point.radial % spacing) *
                      detail::simpleFullScale) /
                     spacing) +
                    phaseOffset);
                const auto band = detail::highBand(
                    Primitives::FieldMath::triangle8(ringPhase), bandWidth);
                if (band == 0) {
                    return;
                }
                const auto breath = config.direction == 2
                                        ? Primitives::FieldMath::sine8Q8(
                                              static_cast<uint16_t>(progress) *
                                              detail::simpleQ8Unit)
                                        : detail::simpleFullScale;
                const auto value = detail::scale2(config.value, band, breath);
                if (value == 0) {
                    return;
                }
                const auto ringIndex = static_cast<uint8_t>(
                    spacing == 0 ? 0 : point.radial / spacing);
                ctx.canvas.pixel(
                    point.spoke, point.radial,
                    HsvColor{.hue = static_cast<uint8_t>(
                                 hue + (ringIndex * config.hueStep)),
                             .saturation = config.saturation,
                             .value = value});
            });
    }

  private:
    [[nodiscard]] uint8_t directionPhase(uint8_t progress) const {
        switch (config.direction) {
        case 1:
            return static_cast<uint8_t>(-progress);
        case 2:
            return 0;
        default:
            return progress;
        }
    }
};

} // namespace Totem::LedDisplay::Animations
