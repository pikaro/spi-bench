#pragma once

#include "LedDisplay/Animations/StainedCells/Config.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace Totem::LedDisplay::Animations {

struct StainedCells {
    static constexpr AnimationKind kind = StainedCellsSpec::kind;
    static constexpr Layer defaultLayer = StainedCellsSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        StainedCellsSpec::defaultLifetimeMs;
    static constexpr bool requiresFullFrame = false;
    static constexpr uint8_t maximumSeeds = 7;
    static constexpr uint8_t minimumSeeds = 1;
    static constexpr uint8_t minimumBorderWidth = 4;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::Replace,
                                                 .opacity = 255};

    StainedCellsConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto seedCount = boundedSeedCount(config.seedCount);
        const auto baseHue =
            static_cast<uint8_t>(config.baseHue + ctx.hueOffset);
        const auto bass = audioValue(ctx.audio.bass, 32, ctx.audio.hasInput);
        const auto mid = audioValue(ctx.audio.mid, 28, ctx.audio.hasInput);
        const auto high = audioValue(ctx.audio.high, 24, ctx.audio.hasInput);
        const auto energy =
            audioValue(ctx.audio.energy, 32, ctx.audio.hasInput);
        const auto bassShape = Primitives::FieldMath::smoothstep8(bass);
        const auto midShape = Primitives::FieldMath::smoothstep8(mid);
        const auto highShape = Primitives::FieldMath::smoothstep8(high);
        const auto highPeak = Primitives::FieldMath::scale8(
            ctx.audio.highAttack, config.peakSensitivity);
        const auto phase = static_cast<uint16_t>(
            (static_cast<uint32_t>(ctx.clock.nowMs) * config.driftSpeed) >>
            5U);
        const auto phase8 = static_cast<uint8_t>(phase >> 8U);
        const auto seeds = makeSeeds(seedCount, phase8, midShape);
        const auto borderWidth =
            std::max<uint8_t>(config.borderWidth, minimumBorderWidth);
        const auto borderLimit =
            static_cast<uint32_t>(borderWidth) * borderWidth;
        const auto interior = Primitives::FieldMath::scale8(
            config.interiorValue,
            Primitives::FieldMath::qadd8(
                96, Primitives::FieldMath::scale8(bassShape, 96)));
        const auto borderGain = Primitives::FieldMath::qadd8(
            highShape, highPeak);
        const auto audioScale = Primitives::FieldMath::qadd8(
            112, Primitives::FieldMath::scale8(energy, 80));
        const auto spectralHue = static_cast<uint16_t>(
            Primitives::FieldMath::scale8(bassShape, 20) +
            Primitives::FieldMath::scale8(midShape, 72) +
            Primitives::FieldMath::scale8(highShape, 128));

        Primitives::forEachLogicalPixel([&](const auto point) {
            const auto nearest = nearestSeeds(point, seeds, seedCount);
            const auto delta = nearest.secondDistance > nearest.firstDistance
                                   ? nearest.secondDistance -
                                         nearest.firstDistance
                                   : 0U;
            const auto border =
                delta >= borderLimit
                    ? 0
                    : Primitives::FieldMath::smoothstep8(
                          Primitives::FieldMath::clampU8(static_cast<uint16_t>(
                              Primitives::FieldMath::fullScale -
                              ((delta * Primitives::FieldMath::fullScale) /
                               borderLimit))));
            auto field = Primitives::FieldMath::qadd8(
                interior, Primitives::FieldMath::scale8(border, borderGain));
            field = Primitives::FieldMath::scale8(field, config.contrast);
            field = Primitives::FieldMath::scale8(field, audioScale);
            auto value = Primitives::FieldMath::qadd8(
                config.baseValue,
                Primitives::FieldMath::scale8(config.value, field));
            if (!ctx.audio.hasInput) {
                value = Primitives::FieldMath::scale8(value, 152);
            }
            if (value == 0) {
                return;
            }

            const auto hueOffset = scaleHueModulation(
                static_cast<uint16_t>(
                    spectralHue + seeds[nearest.index].hueOffset +
                    Primitives::FieldMath::scale8(border, 48)),
                config.hueModulation);
            ctx.canvas.pixel(point.spoke, point.radial,
                             HsvColor{.hue = static_cast<uint8_t>(baseHue +
                                                                   hueOffset),
                                      .saturation = config.saturation,
                                      .value = value});
        });
    }

  private:
    struct Seed {
        uint8_t theta = 0;
        uint8_t radius = 0;
        uint8_t hueOffset = 0;
    };

    struct NearestSeeds {
        uint32_t firstDistance = std::numeric_limits<uint32_t>::max();
        uint32_t secondDistance = std::numeric_limits<uint32_t>::max();
        uint8_t index = 0;
    };

    [[nodiscard]] static constexpr uint8_t boundedSeedCount(uint8_t count) {
        if (count < minimumSeeds) {
            return minimumSeeds;
        }
        return count > maximumSeeds ? maximumSeeds : count;
    }

    [[nodiscard]] static constexpr uint8_t audioValue(uint8_t value,
                                                      uint8_t fallback,
                                                      bool hasInput) {
        return hasInput ? value : fallback;
    }

    [[nodiscard]] std::array<Seed, maximumSeeds>
    makeSeeds(uint8_t seedCount, uint8_t phase, uint8_t mid) const {
        std::array<Seed, maximumSeeds> seeds{};
        const auto drift = Primitives::FieldMath::scale8(mid, 48);
        for (uint8_t index = 0; index < seedCount; ++index) {
            const auto thetaHash = Primitives::FieldMath::hash8(
                config.seed, static_cast<uint8_t>(index * 41U), phase);
            const auto radiusHash = Primitives::FieldMath::hash8(
                static_cast<uint8_t>(config.seed ^ 0xC3U),
                static_cast<uint8_t>(index * 53U), phase);
            seeds[index] = Seed{
                .theta = static_cast<uint8_t>(
                    thetaHash +
                    static_cast<uint8_t>(
                        (static_cast<uint16_t>(index) * 256U) / seedCount) +
                    drift),
                .radius = static_cast<uint8_t>(
                    24U + Primitives::FieldMath::scale8(radiusHash, 200)),
                .hueOffset = static_cast<uint8_t>(
                    index * 37U + Primitives::FieldMath::scale8(thetaHash, 32)),
            };
        }
        return seeds;
    }

    [[nodiscard]] static NearestSeeds
    nearestSeeds(const Primitives::FieldPoint &point,
                 const std::array<Seed, maximumSeeds> &seeds,
                 uint8_t seedCount) {
        auto nearest = NearestSeeds{};
        const auto radius = static_cast<uint8_t>(point.stripRadius >> 8U);
        const auto thetaWeight = static_cast<uint8_t>(
            64U + ((static_cast<uint16_t>(radius) * 96U) /
                   Primitives::FieldMath::fullScale));
        for (uint8_t index = 0; index < seedCount; ++index) {
            const auto thetaDistance = Primitives::FieldMath::scale8(
                Primitives::FieldMath::angularDistance(point.theta,
                                                       seeds[index].theta),
                thetaWeight);
            const auto radiusDistance =
                Primitives::FieldMath::absDiff8(radius, seeds[index].radius);
            const auto distance =
                static_cast<uint32_t>(thetaDistance) * thetaDistance +
                static_cast<uint32_t>(radiusDistance) * radiusDistance;
            if (distance < nearest.firstDistance) {
                nearest.secondDistance = nearest.firstDistance;
                nearest.firstDistance = distance;
                nearest.index = index;
            } else if (distance < nearest.secondDistance) {
                nearest.secondDistance = distance;
            }
        }
        return nearest;
    }

    [[nodiscard]] static constexpr uint8_t
    scaleHueModulation(uint16_t value, uint8_t modulation) {
        return static_cast<uint8_t>(
            (static_cast<uint32_t>(value) * modulation) /
            Primitives::FieldMath::fullScale);
    }
};

} // namespace Totem::LedDisplay::Animations
