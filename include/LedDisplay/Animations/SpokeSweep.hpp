#pragma once

#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#include "Macros/Facade.hpp"
#include "Macros/internal/Markers.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <limits>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG SpokeSweepConfig {
    uint8_t baseHue = 0;
    uint8_t hueStride = 16;
    uint8_t value = 220;
    uint8_t trailSpokes = 2;
    uint8_t cycles = 1;
    uint8_t markerValue = 255;
};

struct SpokeSweep {
    static constexpr AnimationKind kind = AnimationKind::SpokeSweep;
    static constexpr Layer defaultLayer = Layer::Debug;
    static constexpr uint16_t defaultLifetimeMs = 6000;
    static constexpr uint16_t defaultRequestId = 2;
    static constexpr uint8_t minimumCycles = 1;
    static constexpr uint8_t fullScale =
        std::numeric_limits<uint8_t>::max();
    static constexpr uint8_t markerSaturation = 255;
    static constexpr uint8_t innerOuterMarkerHue = 96;
    static constexpr uint8_t secondaryMarkerHue = 0;
    static constexpr uint8_t neutralMarkerSaturation = 0;
    static constexpr uint8_t markerBandCount = 3;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::Replace,
                                                 .opacity = 255};
    static_assert(Config::ringCount >= markerBandCount,
                  "Spoke sweep markers require at least three radial rings");

    SpokeSweepConfig config{};

    static std::expected<AnimationCommand, ReturnCode>
    makeCommand(SpokeSweepConfig commandConfig = {},
                uint16_t requestId = defaultRequestId,
                uint16_t lifetimeMs = defaultLifetimeMs,
                Layer layer = defaultLayer);

    void render(AnimationRenderContext &ctx) const {
        const auto duration = nonzero(ctx.clock.durationMs, defaultLifetimeMs);
        const auto cycleCount = std::max<uint8_t>(config.cycles,
                                                 minimumCycles);
        const auto totalSweepSteps =
            static_cast<uint32_t>(Config::spokeCount) * cycleCount;
        const auto sweepStep = static_cast<uint32_t>(
            (static_cast<uint64_t>(ctx.clock.elapsedMs) * totalSweepSteps) /
            duration);
        const auto activeSpoke =
            static_cast<uint8_t>(sweepStep % Config::spokeCount);
        const auto visibleTrailSpokes =
            std::min<uint8_t>(config.trailSpokes, Config::spokeCount - 1U);

        for (uint8_t distance = 0; distance <= visibleTrailSpokes;
             ++distance) {
            const auto value =
                Renderers::GenericRenderer::scale8(config.value,
                                                   trailScale(distance,
                                                              visibleTrailSpokes));
            if (value == 0) {
                continue;
            }
            const auto spoke = wrapSpoke(activeSpoke, -distance);
            const auto hue = static_cast<uint8_t>(
                config.baseHue + (spoke * config.hueStride) + ctx.hueOffset);
            drawMarkedSpoke(ctx, spoke, hue, value, config.markerValue,
                            distance == 0);
        }
    }

  private:
    [[nodiscard]] static constexpr uint32_t nonzero(uint32_t value,
                                                    uint32_t fallback) {
        return value == 0 ? fallback : value;
    }

    [[nodiscard]] static constexpr uint8_t
    wrapSpoke(uint8_t center, int16_t offset) {
        const auto raw = static_cast<int16_t>(center) + offset;
        const auto spokeCount = static_cast<int16_t>(Config::spokeCount);
        const auto wrapped = ((raw % spokeCount) + spokeCount) % spokeCount;
        return static_cast<uint8_t>(wrapped);
    }

    [[nodiscard]] static constexpr uint8_t trailScale(uint8_t distance,
                                                      uint8_t trailSpokes) {
        if (trailSpokes == 0) {
            return distance == 0 ? fullScale : 0;
        }
        const auto visibleSteps =
            static_cast<uint16_t>(trailSpokes - distance + 1U);
        const auto fadeStepsIncludingTail =
            static_cast<uint16_t>(trailSpokes) + 1U;
        return static_cast<uint8_t>((visibleSteps * fullScale) /
                                    fadeStepsIncludingTail);
    }

    static void drawMarkedSpoke(AnimationRenderContext &ctx, uint8_t spoke,
                                uint8_t hue, uint8_t value,
                                uint8_t markerValue, bool active) {
        ctx.canvas.spoke(spoke, HsvColor{.hue = hue,
                                         .saturation = markerSaturation,
                                         .value = value},
                         BlendOp::Replace);

        if (!active) {
            return;
        }

        const auto activeMarkerValue = std::max(markerValue, value);
        const auto lastRadial =
            static_cast<uint8_t>(Config::ringCount - 1U);
        const auto markerColors = std::array<HsvColor, markerBandCount>{
            HsvColor{.hue = innerOuterMarkerHue,
                     .saturation = markerSaturation,
                     .value = activeMarkerValue},
            HsvColor{.hue = secondaryMarkerHue,
                     .saturation = markerSaturation,
                     .value = activeMarkerValue},
            HsvColor{.hue = 0,
                     .saturation = neutralMarkerSaturation,
                     .value = activeMarkerValue},
        };

        for (uint8_t marker = 0; marker < markerBandCount; ++marker) {
            ctx.canvas.pixel(spoke, marker, markerColors[marker],
                             BlendOp::Replace);
            ctx.canvas.pixel(spoke,
                             static_cast<uint8_t>(lastRadial - marker),
                             markerColors[marker], BlendOp::Replace);
        }
    }
};

static_assert(std::is_trivially_copyable_v<SpokeSweepConfig>,
              "SpokeSweepConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
