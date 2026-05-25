#pragma once

#include "LedDisplay/Animations/Sinelon/Config.hpp"
#include "LedDisplay/Animations/detail/WaveGeometry.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#include "LedDisplay/Renderers/Waveform.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace Totem::LedDisplay::Animations {

struct Sinelon {
    static constexpr AnimationKind kind = SinelonSpec::kind;
    static constexpr Layer defaultLayer = SinelonSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        SinelonSpec::defaultLifetimeMs;
    static constexpr uint16_t defaultPeriodMs = SinelonSpec::defaultPeriodMs;
    static constexpr uint16_t minimumPeriodMs = SinelonSpec::minimumPeriodMs;
    static constexpr uint8_t minimumWidth = SinelonSpec::minimumWidth;
    static constexpr uint8_t defaultSpokeGainPhaseStep =
        SinelonSpec::defaultSpokeGainPhaseStep;
    static constexpr uint8_t fullScale = std::numeric_limits<uint8_t>::max();
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    SinelonConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto period =
            std::max<uint16_t>(config.periodMs, minimumPeriodMs);
        const auto headDistance = radialDistanceQ8(
            detail::phase8(ctx.clock.elapsedMs, period), config.travelRings);
        const auto head =
            detail::originRadialQ8(headDistance, config.outerOrigin);
        const auto radius =
            static_cast<uint16_t>(std::max(config.width, minimumWidth)) *
            detail::q8Unit;
        const auto attenuation = detail::repeatedAttenuation(
            bounceCount(ctx.clock.elapsedMs, period), config.bounceAttenuation);
        const auto hue = static_cast<uint8_t>(config.hue + ctx.hueOffset);

        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            const auto center = static_cast<uint16_t>(radial) * detail::q8Unit;
            const auto distance = detail::distanceQ8(center, head);
            const auto scale = detail::lobeScale(distance, radius);
            if (scale == 0) {
                continue;
            }
            auto value =
                Renderers::GenericRenderer::scale8(config.value, scale);
            value = Renderers::GenericRenderer::scale8(value, attenuation);
            if (value == 0) {
                continue;
            }
            for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
                const auto gained =
                    detail::applySpokeGain(value, spoke, config.spokeGainPct,
                                           config.spokeGainPhaseStep);
                if (gained == 0) {
                    continue;
                }
                ctx.canvas.pixel(spoke, radial,
                                 HsvColor{.hue = hue,
                                          .saturation = config.saturation,
                                          .value = gained});
            }
        }
    }

  private:
    [[nodiscard]] static uint16_t radialDistanceQ8(uint8_t phase,
                                                   uint8_t travelRings) {
        const auto maxDistance = detail::maxTravelDistanceQ8(travelRings);
        const auto position =
            Renderers::Waveform::sine8(static_cast<uint8_t>(phase + 192U));
        return static_cast<uint16_t>(
            (static_cast<uint32_t>(position) * maxDistance) / fullScale);
    }

    [[nodiscard]] static uint32_t bounceCount(uint32_t elapsedMs,
                                              uint16_t periodMs) {
        const auto halfPeriod = std::max<uint16_t>(periodMs / 2U, 1U);
        return elapsedMs / halfPeriod;
    }
};

} // namespace Totem::LedDisplay::Animations
