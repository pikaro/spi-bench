#pragma once

#include "LedDisplay/Animations/SpectralIris/Config.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct SpectralIris {
    static constexpr AnimationKind kind = SpectralIrisSpec::kind;
    static constexpr Layer defaultLayer = SpectralIrisSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        SpectralIrisSpec::defaultLifetimeMs;
    static constexpr bool requiresFullFrame = false;
    static constexpr uint8_t minimumPetals = 1;
    static constexpr uint8_t maximumPetals = 8;
    static constexpr uint8_t minimumRimWidth = 6;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::Replace,
                                                 .opacity = 255};

    SpectralIrisConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto baseHue =
            static_cast<uint8_t>(config.baseHue + ctx.hueOffset);
        const auto petals = boundedPetals(config.petals);
        const auto phaseQ8 = static_cast<uint16_t>(
            (static_cast<uint32_t>(ctx.clock.nowMs) * config.flowSpeed) >>
            2U);
        const auto phase = static_cast<uint8_t>(phaseQ8 >> 8U);
        const auto bass = audioValue(ctx.audio.bass, 40, ctx.audio.hasInput);
        const auto mid = audioValue(ctx.audio.mid, 32, ctx.audio.hasInput);
        const auto high = audioValue(ctx.audio.high, 24, ctx.audio.hasInput);
        const auto energy =
            audioValue(ctx.audio.energy, 36, ctx.audio.hasInput);
        const auto bassShape = Primitives::FieldMath::smoothstep8(bass);
        const auto midShape = Primitives::FieldMath::smoothstep8(mid);
        const auto highShape = Primitives::FieldMath::smoothstep8(high);
        const auto highPeak = Primitives::FieldMath::scale8(
            ctx.audio.highAttack, config.peakSensitivity);
        const auto audioScale = Primitives::FieldMath::qadd8(
            112, Primitives::FieldMath::scale8(energy, 96));
        const auto apertureOffset =
            Primitives::FieldMath::scale8(bassShape, 64);
        const auto apertureCenter = Primitives::FieldMath::clampU8(
            static_cast<uint16_t>(config.aperture) + apertureOffset);
        const auto apertureCenterQ8 =
            static_cast<uint16_t>(apertureCenter) << 8U;
        const auto rimWidthQ8 =
            static_cast<uint16_t>(
                std::max<uint8_t>(config.rimWidth, minimumRimWidth))
            << 8U;
        const auto petalGain = Primitives::FieldMath::qadd8(
            96, Primitives::FieldMath::scale8(midShape, 128));
        const auto rimGain = Primitives::FieldMath::qadd8(
            128, Primitives::FieldMath::qadd8(highShape, highPeak));
        const auto hueBase = static_cast<uint16_t>(
            Primitives::FieldMath::scale8(bassShape, 16) +
            Primitives::FieldMath::scale8(midShape, 64) +
            Primitives::FieldMath::scale8(highShape, 112));

        Primitives::forEachLogicalPixel([&](const auto point) {
            const auto folded = Primitives::FieldMath::foldedAngle(
                static_cast<uint8_t>(point.theta + phase), petals);
            const auto petal = Primitives::FieldMath::smoothstep8(
                Primitives::FieldMath::triangle8(folded));
            const auto rim = Primitives::FieldMath::ringPulseQ8(
                point.stripRadius, apertureCenterQ8, rimWidthQ8);
            const auto inside = point.stripRadius < apertureCenterQ8
                                    ? Primitives::FieldMath::scale8(
                                          bassShape, 72)
                                    : 0;
            auto field = Primitives::FieldMath::qadd8(
                Primitives::FieldMath::scale8(rim, rimGain),
                Primitives::FieldMath::scale8(petal, petalGain));
            field = Primitives::FieldMath::qadd8(field, inside);
            field = Primitives::FieldMath::scale8(field, config.contrast);
            field = Primitives::FieldMath::scale8(field, audioScale);

            auto value = Primitives::FieldMath::qadd8(
                config.baseValue,
                Primitives::FieldMath::scale8(config.value, field));
            if (!ctx.audio.hasInput) {
                value = Primitives::FieldMath::scale8(value, 160);
            }
            if (value == 0) {
                return;
            }

            const auto radius = static_cast<uint8_t>(point.stripRadius >> 8U);
            const auto hueOffset = scaleHueModulation(
                static_cast<uint16_t>(
                    hueBase + Primitives::FieldMath::scale8(petal, 48) +
                    Primitives::FieldMath::scale8(radius, 18)),
                config.hueModulation);
            ctx.canvas.pixel(point.spoke, point.radial,
                             HsvColor{.hue = static_cast<uint8_t>(baseHue +
                                                                   hueOffset),
                                      .saturation = config.saturation,
                                      .value = value});
        });
    }

  private:
    [[nodiscard]] static constexpr uint8_t boundedPetals(uint8_t petals) {
        if (petals < minimumPetals) {
            return minimumPetals;
        }
        return petals > maximumPetals ? maximumPetals : petals;
    }

    [[nodiscard]] static constexpr uint8_t audioValue(uint8_t value,
                                                      uint8_t fallback,
                                                      bool hasInput) {
        return hasInput ? value : fallback;
    }

    [[nodiscard]] static constexpr uint8_t
    scaleHueModulation(uint16_t value, uint8_t modulation) {
        return static_cast<uint8_t>(
            (static_cast<uint32_t>(value) * modulation) /
            Primitives::FieldMath::fullScale);
    }
};

} // namespace Totem::LedDisplay::Animations
