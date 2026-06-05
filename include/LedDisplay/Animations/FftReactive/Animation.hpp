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
    static constexpr uint8_t defaultSymmetry = 1;
    static constexpr uint8_t ringPulseWidth = 26;
    static constexpr uint8_t bassRingInner = 44;
    static constexpr uint8_t bassRingTravel = 168;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::Replace,
                                                 .opacity = 255};

    FftReactiveConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto baseHue =
            static_cast<uint8_t>(config.baseHue + ctx.hueOffset);
        const auto radialMode = nonzero(config.radialMode, minimumMode);
        const auto angularMode = nonzero(config.angularMode, minimumMode);
        const auto highAngularMode = decorrelatedHighAngularMode(angularMode);
        const auto symmetry = boundedSymmetry(config.symmetry);
        const auto phaseQ8 = static_cast<uint16_t>(
            (static_cast<uint32_t>(ctx.clock.nowMs) * config.flowSpeed) >>
            2U);
        const auto ringCenterQ8 = static_cast<uint16_t>(
            (static_cast<uint16_t>(bassRingInner) << 8U) +
            ((static_cast<uint32_t>(Primitives::FieldMath::triangle8Q8(
                  phaseQ8)) *
              bassRingTravel * 256U) /
             Primitives::FieldMath::fullScale));
        const auto energy =
            Primitives::FieldMath::smoothstep8(ctx.audio.energy);
        const auto bass = Primitives::FieldMath::smoothstep8(ctx.audio.bass);
        const auto mid = Primitives::FieldMath::smoothstep8(ctx.audio.mid);
        const auto high = Primitives::FieldMath::smoothstep8(ctx.audio.high);
        const auto spectralTotal = static_cast<uint16_t>(ctx.audio.bass) +
                                   ctx.audio.mid + ctx.audio.high;
        const auto bassShape = Primitives::FieldMath::scale8(
            Primitives::FieldMath::smoothstep8(
                spectralWeight(ctx.audio.bass, spectralTotal)),
            energy);
        const auto midShape = Primitives::FieldMath::scale8(
            Primitives::FieldMath::smoothstep8(
                spectralWeight(ctx.audio.mid, spectralTotal)),
            energy);
        const auto highShape = Primitives::FieldMath::scale8(
            Primitives::FieldMath::smoothstep8(
                spectralWeight(ctx.audio.high, spectralTotal)),
            energy);
        const auto colorBass = colorControl(ctx.audio.bass, bassShape);
        const auto colorMid = colorControl(ctx.audio.mid, midShape);
        const auto colorHigh = colorControl(ctx.audio.high, highShape);
        const auto bassPeak = Primitives::FieldMath::scale8(
            ctx.audio.bassAttack, config.peakSensitivity);
        const auto midHighPeak = Primitives::FieldMath::scale8(
            Primitives::FieldMath::average2(ctx.audio.midAttack,
                                            ctx.audio.highAttack),
            static_cast<uint8_t>(config.peakSensitivity >> 1U));
        const auto audioScale = Primitives::FieldMath::qadd8(
            112, Primitives::FieldMath::scale8(energy, 96));
        const auto bassPattern =
            patternControl(bass, bassShape);
        const auto midPattern =
            patternControl(mid, midShape);
        const auto highPattern =
            patternControl(high, highShape);
        const auto radialScale = Primitives::FieldMath::qadd8(
            96, Primitives::FieldMath::scale8(bassPattern, 128));
        const auto angularScale = Primitives::FieldMath::qadd8(
            96, Primitives::FieldMath::scale8(midPattern, 128));
        const auto highAccent = Primitives::FieldMath::scale8(highPattern, 72);
        const auto bassHueDepth =
            Primitives::FieldMath::scale8(colorBass, 64);
        const auto midHueDepth =
            Primitives::FieldMath::scale8(colorMid, 112);
        const auto highHueDepth =
            Primitives::FieldMath::scale8(colorHigh, 160);
        const auto bandHueBase = static_cast<uint16_t>(
            Primitives::FieldMath::scale8(colorBass, 4) +
            Primitives::FieldMath::scale8(colorMid, 56) +
            Primitives::FieldMath::scale8(colorHigh, 104));

        Primitives::forEachLogicalPixel([&](const auto point) {
            const auto radius = static_cast<uint8_t>(point.stripRadius >> 8U);
            const auto patternTheta =
                symmetry <= 1 ? point.theta
                              : Primitives::FieldMath::foldedAngle(point.theta,
                                                                   symmetry);
            const auto radialWave =
                standingWaveQ8(point, radialMode, 0, phaseQ8);
            const auto angularPhaseQ8 = static_cast<uint16_t>(
                ((static_cast<uint32_t>(patternTheta) * angularMode * 256U) +
                 (phaseQ8 >> 1U)) &
                0xFFFFU);
            const auto angularWave =
                Primitives::FieldMath::sine8Q8(angularPhaseQ8);
            const auto highPhaseQ8 = static_cast<uint16_t>(
                ((static_cast<uint32_t>(point.theta) *
                  highAngularMode * 256U) +
                 ((static_cast<uint32_t>(point.stripRadius) *
                   (static_cast<uint16_t>(radialMode) + 1U)) >>
                  1U) -
                 phaseQ8) &
                0xFFFFU);
            const auto highWave =
                Primitives::FieldMath::sine8Q8(highPhaseQ8);
            auto field = Primitives::FieldMath::average2(
                Primitives::FieldMath::scale8(radialWave, radialScale),
                Primitives::FieldMath::scale8(angularWave, angularScale));
            field = Primitives::FieldMath::scale8(field, config.contrast);
            field = Primitives::FieldMath::scale8(field, audioScale);
            field = Primitives::FieldMath::qadd8(
                field,
                Primitives::FieldMath::scale8(angularWave, highAccent));
            field = Primitives::FieldMath::qadd8(
                field, Primitives::FieldMath::scale8(
                           Primitives::FieldMath::ringPulseQ8(
                               point.stripRadius, ringCenterQ8,
                               static_cast<uint16_t>(
                                   (ringPulseWidth + (bassPeak >> 4U))
                                   << 8U)),
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

            const auto hueField = static_cast<uint16_t>(
                (static_cast<uint16_t>(
                     Primitives::FieldMath::scale8(radialWave, bassHueDepth)) +
                 Primitives::FieldMath::scale8(angularWave, midHueDepth) +
                 Primitives::FieldMath::scale8(highWave, highHueDepth)) /
                2U);
            const auto spatialHue = static_cast<uint16_t>(
                spatialHueTexture(point) +
                Primitives::FieldMath::scale8(point.theta, 24) +
                Primitives::FieldMath::scale8(radius, 18));
            const auto hueOffset = scaleHueModulation(
                static_cast<uint16_t>(bandHueBase + hueField + spatialHue),
                config.hueModulation);
            const auto hue = static_cast<uint8_t>(
                baseHue + hueOffset);
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

    [[nodiscard]] static constexpr uint16_t
    decorrelatedHighAngularMode(uint8_t angularMode) {
        const auto candidate = static_cast<uint16_t>(angularMode) + 2U;
        return (candidate & 1U) == 0 ? candidate + 1U : candidate;
    }

    [[nodiscard]] static uint8_t standingWaveQ8(
        const Primitives::FieldPoint &point, uint8_t radialMode,
        uint8_t angularMode, uint16_t phaseQ8) {
        const auto radialPhaseQ8 = static_cast<uint16_t>(
            (static_cast<uint32_t>(point.stripRadius) * radialMode) &
            0xFFFFU);
        const auto angularPhaseQ8 = static_cast<uint16_t>(
            (static_cast<uint32_t>(point.theta) * angularMode * 256U) &
            0xFFFFU);
        return Primitives::FieldMath::sine8Q8(static_cast<uint16_t>(
            radialPhaseQ8 + angularPhaseQ8 + phaseQ8));
    }

    [[nodiscard]] static uint8_t expandColorControl(uint8_t value) {
        return Primitives::FieldMath::qadd8(
            value, Primitives::FieldMath::scale8(
                       value, static_cast<uint8_t>(255U - value)));
    }

    [[nodiscard]] static constexpr uint8_t spectralWeight(uint8_t value,
                                                          uint16_t total) {
        constexpr uint16_t minimumSpectralTotal = 12;
        if (total < minimumSpectralTotal) {
            return 0;
        }
        return Primitives::FieldMath::clampU8(
            static_cast<uint16_t>((static_cast<uint32_t>(value) *
                                   Primitives::FieldMath::fullScale) /
                                  total));
    }

    [[nodiscard]] static uint8_t colorControl(uint8_t absolute,
                                              uint8_t spectralShape) {
        return Primitives::FieldMath::qadd8(
            Primitives::FieldMath::scale8(expandColorControl(absolute), 120),
            Primitives::FieldMath::scale8(spectralShape, 160));
    }

    [[nodiscard]] static uint8_t patternControl(uint8_t absolute,
                                                uint8_t spectralShape) {
        return Primitives::FieldMath::qadd8(
            Primitives::FieldMath::scale8(absolute, 128),
            Primitives::FieldMath::scale8(spectralShape, 128));
    }

    [[nodiscard]] static constexpr uint8_t
    scaleHueModulation(uint16_t value, uint8_t modulation) {
        return static_cast<uint8_t>(
            (static_cast<uint32_t>(value) * modulation) /
            Primitives::FieldMath::fullScale);
    }

    [[nodiscard]] static constexpr uint8_t spatialHueTexture(
        const Primitives::FieldPoint &point) {
        auto hash = static_cast<uint8_t>(point.spoke * 37U);
        hash = static_cast<uint8_t>(hash ^ static_cast<uint8_t>(point.radial *
                                                                19U));
        hash = static_cast<uint8_t>(hash ^ static_cast<uint8_t>(hash >> 3U));
        return static_cast<uint8_t>(hash & 0x0FU);
    }
};

} // namespace Totem::LedDisplay::Animations
