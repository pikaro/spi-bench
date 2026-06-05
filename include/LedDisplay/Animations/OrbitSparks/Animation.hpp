#pragma once

#include "LedDisplay/Animations/OrbitSparks/Config.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct OrbitSparks {
    static constexpr AnimationKind kind = OrbitSparksSpec::kind;
    static constexpr Layer defaultLayer = OrbitSparksSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        OrbitSparksSpec::defaultLifetimeMs;
    static constexpr bool requiresFullFrame = false;
    static constexpr uint8_t maximumSparks = 48;
    static constexpr uint8_t maximumSparkSize = 1;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    OrbitSparksConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto count = boundedSparkCount(config.sparkCount);
        const auto size = std::min<uint8_t>(config.sparkSize, maximumSparkSize);
        const auto baseHue =
            static_cast<uint8_t>(config.baseHue + ctx.hueOffset);
        const auto bass = audioValue(ctx.audio.bass, 32, ctx.audio.hasInput);
        const auto mid = audioValue(ctx.audio.mid, 28, ctx.audio.hasInput);
        const auto high = audioValue(ctx.audio.high, 24, ctx.audio.hasInput);
        const auto highPeak = Primitives::FieldMath::scale8(
            ctx.audio.highAttack, config.peakSensitivity);
        const auto phase = static_cast<uint16_t>(
            (static_cast<uint32_t>(ctx.clock.nowMs) * config.orbitSpeed) >>
            4U);
        const auto phase8 = static_cast<uint8_t>(phase >> 8U);
        const auto midDrift = Primitives::FieldMath::scale8(mid, 48);
        const auto radialPush = static_cast<uint8_t>(
            (static_cast<uint16_t>(
                 Primitives::FieldMath::scale8(bass, config.radialDrift)) *
             Config::ringCount) /
            512U);
        const auto sparkle = Primitives::FieldMath::qadd8(
            Primitives::FieldMath::scale8(high, config.highSparkle), highPeak);
        const auto valueScale =
            Primitives::FieldMath::qadd8(96, sparkle);
        const auto spectralHue = static_cast<uint16_t>(
            Primitives::FieldMath::scale8(bass, 24) +
            Primitives::FieldMath::scale8(mid, 72) +
            Primitives::FieldMath::scale8(high, 128));

        for (uint8_t index = 0; index < count; ++index) {
            const auto hash = Primitives::FieldMath::hash8(
                config.seed, index, phase8);
            const auto theta = static_cast<uint8_t>(
                hash + static_cast<uint8_t>(
                           (static_cast<uint16_t>(index) * 256U) / count) +
                phase8 + midDrift);
            const auto radialPhase = Primitives::FieldMath::hash8(
                static_cast<uint8_t>(config.seed ^ 0x5AU),
                static_cast<uint8_t>(index * 29U), phase8);
            const auto spoke = static_cast<uint8_t>(
                (static_cast<uint16_t>(theta) * Config::spokeCount) >> 8U);
            auto radial = static_cast<uint8_t>(
                (static_cast<uint16_t>(radialPhase) * Config::ringCount) >>
                8U);
            radial = std::min<uint8_t>(
                static_cast<uint8_t>(Config::ringCount - 1U),
                static_cast<uint8_t>(radial + radialPush));

            auto value = Primitives::FieldMath::scale8(config.value,
                                                       valueScale);
            value = Primitives::FieldMath::qadd8(
                value, Primitives::FieldMath::scale8(hash, sparkle));
            if (!ctx.audio.hasInput) {
                value = Primitives::FieldMath::scale8(value, 144);
            }
            if (value == 0) {
                continue;
            }

            const auto hueOffset = scaleHueModulation(
                static_cast<uint16_t>(spectralHue + hash),
                config.hueModulation);
            drawSpark(ctx, spoke, radial,
                      HsvColor{.hue = static_cast<uint8_t>(baseHue +
                                                           hueOffset),
                               .saturation = config.saturation,
                               .value = value},
                      size);
        }
    }

  private:
    [[nodiscard]] static constexpr uint8_t boundedSparkCount(uint8_t count) {
        if (count == 0) {
            return 1;
        }
        return count > maximumSparks ? maximumSparks : count;
    }

    [[nodiscard]] static constexpr uint8_t audioValue(uint8_t value,
                                                      uint8_t fallback,
                                                      bool hasInput) {
        return hasInput ? value : fallback;
    }

    [[nodiscard]] static constexpr uint8_t wrapSpoke(uint8_t spoke,
                                                     int8_t offset) {
        const auto raw = static_cast<int16_t>(spoke) + offset;
        const auto count = static_cast<int16_t>(Config::spokeCount);
        const auto wrapped = ((raw % count) + count) % count;
        return static_cast<uint8_t>(wrapped);
    }

    static void drawSpark(AnimationRenderContext &ctx, uint8_t spoke,
                          uint8_t radial, HsvColor color, uint8_t size) {
        ctx.canvas.pixel(spoke, radial, color, BlendOp::MaxValue);
        if (size == 0) {
            return;
        }
        if (radial > 0) {
            ctx.canvas.pixel(spoke, static_cast<uint8_t>(radial - 1U), color,
                             BlendOp::MaxValue);
        }
        if (radial + 1U < Config::ringCount) {
            ctx.canvas.pixel(spoke, static_cast<uint8_t>(radial + 1U), color,
                             BlendOp::MaxValue);
        }
        ctx.canvas.pixel(wrapSpoke(spoke, -1), radial, color,
                         BlendOp::MaxValue);
        ctx.canvas.pixel(wrapSpoke(spoke, 1), radial, color,
                         BlendOp::MaxValue);
    }

    [[nodiscard]] static constexpr uint8_t
    scaleHueModulation(uint16_t value, uint8_t modulation) {
        return static_cast<uint8_t>(
            (static_cast<uint32_t>(value) * modulation) /
            Primitives::FieldMath::fullScale);
    }
};

} // namespace Totem::LedDisplay::Animations
