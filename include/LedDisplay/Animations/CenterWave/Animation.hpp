#pragma once

#include "LedDisplay/Animations/CenterWave/Config.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace Totem::LedDisplay::Animations {

struct CenterWave {
    static constexpr AnimationKind kind = CenterWaveSpec::kind;
    static constexpr Layer defaultLayer = CenterWaveSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        CenterWaveSpec::defaultLifetimeMs;
    static constexpr uint8_t minimumPeakRings =
        CenterWaveSpec::minimumPeakRings;
    static constexpr uint16_t fullScale = std::numeric_limits<uint8_t>::max();
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    CenterWaveConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto duration = nonzero(ctx.clock.durationMs, defaultLifetimeMs);
        const auto rise = static_cast<uint32_t>(config.rise);
        const auto peak = std::max<uint32_t>(config.peak, minimumPeakRings);
        const auto wake = static_cast<uint32_t>(config.wake);
        const auto profileRings = rise + peak + wake;
        const auto travelRings =
            static_cast<uint32_t>(Config::ringCount) + profileRings;
        const auto leadingEdgeRing = static_cast<uint32_t>(
            (static_cast<uint64_t>(ctx.clock.elapsedMs) * travelRings) /
            duration);
        const auto hue = static_cast<uint8_t>(config.hue + ctx.hueOffset);

        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            if (leadingEdgeRing < radial) {
                continue;
            }

            const auto distanceBehindLeadingEdge = leadingEdgeRing - radial;
            if (distanceBehindLeadingEdge >= profileRings) {
                continue;
            }

            const auto scale =
                profileScale(distanceBehindLeadingEdge, rise, peak, wake);
            if (scale == 0) {
                continue;
            }

            ctx.canvas.ring(
                radial, HsvColor{.hue = hue,
                                 .saturation = config.saturation,
                                 .value = Renderers::GenericRenderer::scale8(
                                     config.value, scale)});
        }
    }

  private:
    [[nodiscard]] static constexpr uint32_t nonzero(uint32_t value,
                                                    uint32_t fallback) {
        return value == 0 ? fallback : value;
    }

    [[nodiscard]] static constexpr uint8_t
    risingEdgeScale(uint32_t distanceIntoRise, uint32_t riseRings) {
        if (riseRings == 0) {
            return fullScale;
        }
        const auto visibleRiseStep = distanceIntoRise + 1U;
        const auto riseStepsIncludingLeadIn = riseRings + 1U;
        return static_cast<uint8_t>((visibleRiseStep * fullScale) /
                                    riseStepsIncludingLeadIn);
    }

    [[nodiscard]] static constexpr uint8_t wakeScale(uint32_t distanceIntoWake,
                                                     uint32_t wakeRings) {
        if (wakeRings == 0) {
            return 0;
        }
        const auto visibleWakeSteps = wakeRings - distanceIntoWake;
        const auto wakeStepsIncludingTail = wakeRings + 1U;
        return static_cast<uint8_t>((visibleWakeSteps * fullScale) /
                                    wakeStepsIncludingTail);
    }

    [[nodiscard]] static constexpr uint8_t
    profileScale(uint32_t distanceBehindLeadingEdge, uint32_t riseRings,
                 uint32_t peakRings, uint32_t wakeRings) {
        if (distanceBehindLeadingEdge < riseRings) {
            return risingEdgeScale(distanceBehindLeadingEdge, riseRings);
        }

        const auto distanceAfterRise = distanceBehindLeadingEdge - riseRings;
        if (distanceAfterRise < peakRings) {
            return fullScale;
        }

        const auto distanceIntoWake = distanceAfterRise - peakRings;
        return wakeScale(distanceIntoWake, wakeRings);
    }
};

} // namespace Totem::LedDisplay::Animations
