#pragma once

#include "LedDisplay/Animations/Bolt/Config.hpp"
#include "LedDisplay/Animations/detail/SimpleField.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct Bolt {
    static constexpr AnimationKind kind = BoltSpec::kind;
    static constexpr Layer defaultLayer = BoltSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs = BoltSpec::defaultLifetimeMs;
    static constexpr bool requiresFullFrame = false;
    static constexpr uint8_t minimumWidth = BoltSpec::minimumWidth;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    BoltConfig config{};

    void render(AnimationRenderContext &ctx) const {
        constexpr uint8_t phaseStep = 0;
        const auto hue = static_cast<uint8_t>(config.hue + ctx.hueOffset);
        const auto width = std::max<uint8_t>(config.width, minimumWidth);

        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            const auto originDistance =
                config.outerOrigin
                    ? static_cast<uint8_t>((Config::ringCount - 1U) - radial)
                    : radial;
            const auto mainCenter = pathCenter(originDistance, config.seed,
                                               phaseStep, config.jitter);
            drawBoltPoint(ctx, mainCenter, radial, width, config.value, hue);

            if (config.forks != 0 &&
                originDistance > (Config::ringCount / 3U) &&
                originDistance < ((Config::ringCount * 2U) / 3U)) {
                const auto forkCenter = static_cast<uint8_t>(
                    (mainCenter + forkOffset(originDistance, config.seed)) %
                    Config::spokeCount);
                drawBoltPoint(ctx, forkCenter, radial, width,
                              Primitives::FieldMath::scale8(config.value, 128),
                              static_cast<uint8_t>(hue + 8U));
            }
        }
    }

  private:
    [[nodiscard]] uint8_t pathCenter(uint8_t distance, uint8_t seed,
                                     uint8_t phaseStep, uint8_t jitter) const {
        constexpr uint8_t segmentRings = 5;
        const auto amplitude = std::min<uint8_t>(jitter, 4U);
        const auto bendAmplitude =
            std::min<uint8_t>(static_cast<uint8_t>(amplitude + 1U), 4U);
        const auto segment = static_cast<uint8_t>(distance / segmentRings);
        const auto local = static_cast<int16_t>(distance % segmentRings);
        const auto from = signedOffset(seed, segment, phaseStep, amplitude);
        const auto to = signedOffset(seed, static_cast<uint8_t>(segment + 1U),
                                     phaseStep, amplitude);
        const auto offset =
            static_cast<int16_t>(from + (((to - from) * local) / segmentRings));
        const auto bend = static_cast<int16_t>(
            (signedOffset(seed, 0x80, phaseStep, bendAmplitude) *
             static_cast<int16_t>(distance)) /
            static_cast<int16_t>(Config::ringCount - 1U));
        const auto center =
            static_cast<int16_t>(Primitives::FieldMath::hash8(seed, phaseStep) %
                                 Config::spokeCount) +
            offset + bend;
        return wrapSpoke(center);
    }

    [[nodiscard]] static int16_t signedOffset(uint8_t seed, uint8_t segment,
                                              uint8_t phaseStep,
                                              uint8_t amplitude) {
        if (amplitude == 0) {
            return 0;
        }
        const auto span = static_cast<uint8_t>((amplitude * 2U) + 1U);
        return static_cast<int16_t>(
                   Primitives::FieldMath::hash8(seed, segment, phaseStep) %
                   span) -
               static_cast<int16_t>(amplitude);
    }

    [[nodiscard]] static uint8_t wrapSpoke(int16_t value) {
        while (value < 0) {
            value += Config::spokeCount;
        }
        return static_cast<uint8_t>(value % Config::spokeCount);
    }

    [[nodiscard]] static uint8_t forkOffset(uint8_t radial, uint8_t seed) {
        const auto raw = Primitives::FieldMath::hash8(seed, radial, 0x63);
        return (raw & 0x01U) == 0
                   ? 2U
                   : static_cast<uint8_t>(Config::spokeCount - 2U);
    }

    void drawBoltPoint(AnimationRenderContext &ctx, uint8_t center,
                       uint8_t radial, uint8_t width, uint8_t value,
                       uint8_t hue) const {
        for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
            const auto distance = std::min<uint8_t>(
                static_cast<uint8_t>((spoke + Config::spokeCount - center) %
                                     Config::spokeCount),
                static_cast<uint8_t>((center + Config::spokeCount - spoke) %
                                     Config::spokeCount));
            const auto scale = detail::pulseByDistance8(
                static_cast<uint8_t>(distance * detail::simpleQ8Unit /
                                     Config::spokeCount),
                static_cast<uint8_t>(width * detail::simpleQ8Unit /
                                     Config::spokeCount));
            if (scale == 0) {
                continue;
            }
            const auto scaledValue =
                Primitives::FieldMath::scale8(value, scale);
            if (scaledValue == 0) {
                continue;
            }
            ctx.canvas.pixel(spoke, radial,
                             HsvColor{.hue = hue,
                                      .saturation = config.saturation,
                                      .value = scaledValue});
        }
    }
};

} // namespace Totem::LedDisplay::Animations
