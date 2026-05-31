#pragma once

#include "LedDisplay/Animations/WheelIndicator/Config.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace Totem::LedDisplay::Animations {

struct WheelIndicator {
    static constexpr AnimationKind kind = WheelIndicatorSpec::kind;
    static constexpr Layer defaultLayer = WheelIndicatorSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        WheelIndicatorSpec::defaultLifetimeMs;
    static constexpr uint16_t defaultRequestId =
        WheelIndicatorSpec::defaultRequestId;
    static constexpr bool requiresFullFrame = false;
    static constexpr uint32_t wheelAngleSteps =
        static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()) + 1U;
    static constexpr uint8_t minimumSpokes = WheelIndicatorSpec::minimumSpokes;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::Alpha,
                                                 .opacity = 192};
    static constexpr uint16_t defaultWheelPosition = 0;

    WheelIndicatorConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto wheelPosition = ctx.inputs.hasWheelState
                                       ? ctx.inputs.wheelState.position.value
                                       : defaultWheelPosition;
        const auto centerSpoke = spokeForWheelPosition(wheelPosition);
        const auto litSpokes = std::max<uint8_t>(config.spokes, minimumSpokes);
        const auto halfLitSpokes = static_cast<uint8_t>(litSpokes / 2U);
        const auto hue = static_cast<uint8_t>(config.hue + ctx.hueOffset);

        for (uint8_t offset = 0; offset < litSpokes; ++offset) {
            const auto centeredOffset =
                static_cast<int16_t>(offset) - halfLitSpokes;
            const auto spoke = wrapSpoke(centerSpoke, centeredOffset);
            ctx.canvas.spoke(spoke,
                             HsvColor{.hue = hue,
                                      .saturation = config.saturation,
                                      .value = config.value},
                             BlendOp::MaxValue);
        }

        for (uint16_t distance = 1; distance <= config.falloff; ++distance) {
            const auto value = Renderers::GenericRenderer::scale8(
                config.value,
                falloffScale(static_cast<uint8_t>(distance), config.falloff));
            if (value == 0) {
                continue;
            }
            ctx.canvas.spoke(
                wrapSpoke(centerSpoke,
                          -static_cast<int16_t>(
                              static_cast<uint16_t>(halfLitSpokes) + distance)),
                HsvColor{.hue = hue,
                         .saturation = config.saturation,
                         .value = value},
                BlendOp::MaxValue);
            ctx.canvas.spoke(
                wrapSpoke(centerSpoke,
                          static_cast<int16_t>(
                              static_cast<uint16_t>(halfLitSpokes) + distance)),
                HsvColor{.hue = hue,
                         .saturation = config.saturation,
                         .value = value},
                BlendOp::MaxValue);
        }
    }

  private:
    [[nodiscard]] static constexpr uint8_t
    spokeForWheelPosition(uint16_t position) {
        constexpr uint32_t roundToNearestBias = wheelAngleSteps / 2U;
        const auto scaled =
            (static_cast<uint32_t>(position) * Config::spokeCount) +
            roundToNearestBias;
        return static_cast<uint8_t>((scaled / wheelAngleSteps) %
                                    Config::spokeCount);
    }

    [[nodiscard]] static constexpr uint8_t wrapSpoke(uint8_t center,
                                                     int16_t offset) {
        const auto raw = static_cast<int16_t>(center) + offset;
        const auto spokeCount = static_cast<int16_t>(Config::spokeCount);
        const auto wrapped = ((raw % spokeCount) + spokeCount) % spokeCount;
        return static_cast<uint8_t>(wrapped);
    }

    [[nodiscard]] static constexpr uint8_t falloffScale(uint8_t distance,
                                                        uint8_t falloff) {
        if (falloff == 0 || distance > falloff) {
            return 0;
        }
        constexpr uint16_t fullScale = std::numeric_limits<uint8_t>::max();
        const auto remaining = static_cast<uint16_t>(falloff - distance + 1U);
        const auto stepsIncludingTail = static_cast<uint16_t>(falloff) + 1U;
        return static_cast<uint8_t>((remaining * fullScale) /
                                    stepsIncludingTail);
    }
};

} // namespace Totem::LedDisplay::Animations
