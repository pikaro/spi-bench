#pragma once

#include "LedDisplay/Animations/FftReactive/Config.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct FftReactive {
    static constexpr AnimationKind kind = FftReactiveSpec::kind;
    static constexpr Layer defaultLayer = FftReactiveSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        FftReactiveSpec::defaultLifetimeMs;
    static constexpr bool requiresFullFrame = false;
    static constexpr uint8_t minimumMode = 1;
    static constexpr uint8_t maximumSymmetry = 16;
    static constexpr uint8_t defaultSymmetry = 4;
    static constexpr uint8_t ringPulseWidth = 26;
    static constexpr uint8_t bassRingInner = 44;
    static constexpr uint8_t bassRingTravel = 168;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    FftReactiveConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto baseHue =
            static_cast<uint8_t>(config.baseHue + ctx.hueOffset);
        const auto radialMode = nonzero(config.radialMode, minimumMode);
        const auto angularMode = nonzero(config.angularMode, minimumMode);
        const auto symmetry = boundedSymmetry(config.symmetry);
        const auto phase = static_cast<uint8_t>(
            (static_cast<uint32_t>(ctx.clock.nowMs) * config.flowSpeed) >>
            10U);
        const auto ringCenter = static_cast<uint8_t>(
            bassRingInner +
            Primitives::FieldMath::scale8(
                Primitives::FieldMath::triangle8(phase), bassRingTravel));
        const auto energy =
            Primitives::FieldMath::smoothstep8(ctx.audio.energy);
        const auto bassPeak = Primitives::FieldMath::scale8(
            ctx.audio.bassAttack, config.peakSensitivity);
        const auto midHighPeak = Primitives::FieldMath::scale8(
            Primitives::FieldMath::average2(ctx.audio.midAttack,
                                            ctx.audio.highAttack),
            static_cast<uint8_t>(config.peakSensitivity >> 1U));
        const auto audioScale = Primitives::FieldMath::qadd8(
            168, Primitives::FieldMath::scale8(energy, 56));

        Primitives::forEachLogicalPixel([&](const auto point) {
            const auto radius = static_cast<uint8_t>(point.stripRadius >> 8U);
            const auto folded = Primitives::FieldMath::foldedAngle(
                static_cast<uint8_t>(point.theta + (phase >> 2U)), symmetry);
            const auto radialWave = Primitives::FieldMath::standingWave(
                point, radialMode, 0, phase);
            const auto angularWave = Renderers::Waveform::sine8(
                static_cast<uint8_t>((folded * angularMode) + (phase >> 1U)));
            auto field =
                Primitives::FieldMath::average2(radialWave, angularWave);
            field = Primitives::FieldMath::scale8(field, config.contrast);
            field = Primitives::FieldMath::scale8(field, audioScale);
            field = Primitives::FieldMath::qadd8(
                field, Primitives::FieldMath::scale8(
                           Primitives::FieldMath::ringPulse(
                               radius, ringCenter,
                               static_cast<uint8_t>(ringPulseWidth +
                                                    (bassPeak >> 4U))),
                           bassPeak));
            field = Primitives::FieldMath::qadd8(
                field,
                Primitives::FieldMath::scale8(
                    Primitives::FieldMath::average2(radialWave, angularWave),
                    midHighPeak));

            auto value = Primitives::FieldMath::qadd8(
                config.baseValue,
                Primitives::FieldMath::scale8(config.value, field));
            if (!ctx.audio.hasInput) {
                value = Primitives::FieldMath::scale8(value, 160);
            }
            if (value == 0) {
                return;
            }

            const auto hue = static_cast<uint8_t>(
                baseHue + Primitives::FieldMath::scale8(folded, 36) +
                Primitives::FieldMath::scale8(radius, 28) +
                static_cast<uint8_t>(phase >> 2U));
            ctx.canvas.pixel(point.spoke, point.radial,
                             HsvColor{.hue = hue,
                                      .saturation = config.saturation,
                                      .value = value});
        });
    }

  private:
    [[nodiscard]] static constexpr uint8_t nonzero(uint8_t value,
                                                   uint8_t fallback) {
        return value == 0 ? fallback : value;
    }

    [[nodiscard]] static constexpr uint8_t boundedSymmetry(uint8_t value) {
        if (value == 0) {
            return defaultSymmetry;
        }
        return value > maximumSymmetry ? maximumSymmetry : value;
    }
};

} // namespace Totem::LedDisplay::Animations
