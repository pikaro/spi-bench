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
    static constexpr bool requiresFullFrame = false;
    static constexpr uint8_t minimumPeakRings =
        CenterWaveSpec::minimumPeakRings;
    static constexpr uint16_t fullScale = std::numeric_limits<uint8_t>::max();
    static constexpr uint32_t q8Unit = 256U;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    CenterWaveConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto duration = nonzero(ctx.clock.durationMs, defaultLifetimeMs);
        const auto baseRiseQ8 = ringsQ8(config.rise);
        const auto basePeakQ8 =
            ringsQ8(std::max<uint32_t>(config.peak, minimumPeakRings));
        const auto wakeQ8 = ringsQ8(config.wake);
        const auto baseProfileQ8 = baseRiseQ8 + basePeakQ8 + wakeQ8;
        const auto hue = static_cast<uint8_t>(config.hue + ctx.hueOffset);
        const auto baseTravelQ8 = ringsQ8(Config::ringCount) + baseProfileQ8;

        for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
            const auto modulationQ8 =
                spokeModulationQ8(spoke, config.spokeModulo);
            const auto peakQ8 =
                basePeakQ8 + modulatedRingsQ8(config.peakDelta, modulationQ8);
            const auto profileQ8 = baseRiseQ8 + peakQ8 + wakeQ8;
            const auto travelQ8 =
                baseTravelQ8 +
                modulatedRingsQ8(config.speedDelta, modulationQ8);
            const auto leadingEdgeQ8 =
                leadingEdgeDistanceQ8(ctx.clock.elapsedMs, duration, travelQ8);

            for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
                const auto radialQ8 = ringsQ8(radial);
                if (leadingEdgeQ8 < radialQ8) {
                    continue;
                }

                const auto distanceBehindLeadingEdge = leadingEdgeQ8 - radialQ8;
                if (distanceBehindLeadingEdge >= profileQ8) {
                    continue;
                }

                const auto scale = profileScale(distanceBehindLeadingEdge,
                                                baseRiseQ8, peakQ8, wakeQ8);
                if (scale == 0) {
                    continue;
                }

                const auto value =
                    Renderers::GenericRenderer::scale8(config.value, scale);
                if (value == 0) {
                    continue;
                }

                ctx.canvas.pixel(spoke, radial,
                                 HsvColor{.hue = hue,
                                          .saturation = config.saturation,
                                          .value = value});
            }
        }
    }

  private:
    [[nodiscard]] static constexpr uint32_t nonzero(uint32_t value,
                                                    uint32_t fallback) {
        return value == 0 ? fallback : value;
    }

    [[nodiscard]] static constexpr uint32_t ringsQ8(uint32_t rings) {
        return rings * q8Unit;
    }

    [[nodiscard]] static constexpr uint32_t
    modulatedRingsQ8(uint8_t rings, uint16_t modulationQ8) {
        return static_cast<uint32_t>(rings) * modulationQ8;
    }

    [[nodiscard]] static uint32_t leadingEdgeDistanceQ8(uint32_t elapsedMs,
                                                        uint32_t durationMs,
                                                        uint32_t travelQ8) {
        const auto scaled =
            (static_cast<uint64_t>(elapsedMs) * travelQ8) / durationMs;
        return static_cast<uint32_t>(
            std::min<uint64_t>(scaled, std::numeric_limits<uint32_t>::max()));
    }

    [[nodiscard]] static constexpr uint16_t spokeModulationQ8(uint8_t spoke,
                                                              uint8_t modulo) {
        if (modulo <= 1) {
            return 0;
        }

        const auto phase = static_cast<uint16_t>(spoke % modulo);
        const auto mirrored =
            std::min<uint16_t>(phase, static_cast<uint16_t>(modulo - phase));
        return static_cast<uint16_t>(std::min<uint32_t>(
            (static_cast<uint32_t>(mirrored) * 2U * q8Unit) / modulo, q8Unit));
    }

    [[nodiscard]] static constexpr uint8_t
    risingEdgeScale(uint32_t distanceIntoRiseQ8, uint32_t riseQ8) {
        if (riseQ8 == 0) {
            return fullScale;
        }
        const auto visibleRiseQ8 = distanceIntoRiseQ8 + q8Unit;
        const auto riseIncludingLeadInQ8 = riseQ8 + q8Unit;
        return static_cast<uint8_t>(std::min<uint32_t>(
            (visibleRiseQ8 * fullScale) / riseIncludingLeadInQ8, fullScale));
    }

    [[nodiscard]] static constexpr uint8_t
    wakeScale(uint32_t distanceIntoWakeQ8, uint32_t wakeQ8) {
        if (wakeQ8 == 0 || distanceIntoWakeQ8 >= wakeQ8) {
            return 0;
        }
        const auto visibleWakeQ8 = wakeQ8 - distanceIntoWakeQ8;
        const auto wakeIncludingTailQ8 = wakeQ8 + q8Unit;
        return static_cast<uint8_t>((visibleWakeQ8 * fullScale) /
                                    wakeIncludingTailQ8);
    }

    [[nodiscard]] static constexpr uint8_t
    profileScale(uint32_t distanceBehindLeadingEdgeQ8, uint32_t riseQ8,
                 uint32_t peakQ8, uint32_t wakeQ8) {
        if (distanceBehindLeadingEdgeQ8 < riseQ8) {
            return risingEdgeScale(distanceBehindLeadingEdgeQ8, riseQ8);
        }

        const auto distanceAfterRiseQ8 = distanceBehindLeadingEdgeQ8 - riseQ8;
        if (distanceAfterRiseQ8 < peakQ8) {
            return fullScale;
        }

        const auto distanceIntoWakeQ8 = distanceAfterRiseQ8 - peakQ8;
        return wakeScale(distanceIntoWakeQ8, wakeQ8);
    }
};

} // namespace Totem::LedDisplay::Animations
