#pragma once

#include "LedDisplay/Animations/Starburst/Config.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace Totem::LedDisplay::Animations {

struct Starburst {
    static constexpr AnimationKind kind = StarburstSpec::kind;
    static constexpr Layer defaultLayer = StarburstSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        StarburstSpec::defaultLifetimeMs;
    static constexpr bool requiresFullFrame = false;
    static constexpr uint8_t minimumPeakRings = StarburstSpec::minimumPeakRings;
    static constexpr uint8_t minimumPoints = StarburstSpec::minimumPoints;
    static constexpr uint8_t minimumCycles = StarburstSpec::minimumCycles;
    static constexpr uint8_t fullScale = std::numeric_limits<uint8_t>::max();
    static constexpr uint32_t q8Unit = 256U;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    StarburstConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto duration = nonzero(ctx.clock.durationMs, defaultLifetimeMs);
        const auto elapsed =
            cycleElapsedMs(ctx.clock.elapsedMs, duration, config.cycles);
        const auto baseRiseQ8 = ringsQ8(config.rise);
        const auto basePeakQ8 =
            ringsQ8(std::max<uint32_t>(config.peak, minimumPeakRings));
        const auto wakeQ8 = ringsQ8(config.wake);
        const auto baseProfileQ8 = baseRiseQ8 + basePeakQ8 + wakeQ8;
        const auto baseTravelQ8 = ringsQ8(Config::ringCount) + baseProfileQ8;
        const auto hue = static_cast<uint8_t>(config.hue + ctx.hueOffset);
        const auto points = std::max<uint8_t>(config.points, minimumPoints);

        for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
            const auto theta = Primitives::thetaForSpoke(spoke);
            for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
                const auto pointScale =
                    starPointScale(theta, radial, points, config.twist);
                const auto brightnessScale = pointBrightnessScale(pointScale);
                if (brightnessScale == 0) {
                    continue;
                }
                const auto gainQ8 =
                    modulatedRingsQ8(config.pointGain, brightnessScale);
                const auto peakQ8 = basePeakQ8 + gainQ8;
                const auto profileQ8 = baseRiseQ8 + peakQ8 + wakeQ8;
                const auto travelQ8 = baseTravelQ8 + gainQ8;
                const auto leadingEdgeQ8 =
                    leadingEdgeDistanceQ8(elapsed, duration, travelQ8);
                const auto radialQ8 = ringsQ8(radial);
                if (leadingEdgeQ8 < radialQ8) {
                    continue;
                }

                const auto distanceBehindLeadingEdge = leadingEdgeQ8 - radialQ8;
                if (distanceBehindLeadingEdge >= profileQ8) {
                    continue;
                }

                const auto profile = profileScale(distanceBehindLeadingEdge,
                                                  baseRiseQ8, peakQ8, wakeQ8);
                if (profile == 0) {
                    continue;
                }

                const auto value = Renderers::GenericRenderer::scale8(
                    Renderers::GenericRenderer::scale8(config.value, profile),
                    brightnessScale);
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

    [[nodiscard]] static uint32_t
    cycleElapsedMs(uint32_t elapsedMs, uint32_t durationMs, uint8_t cycles) {
        const auto resolvedCycles = std::max<uint8_t>(cycles, minimumCycles);
        if (resolvedCycles <= 1 || durationMs == 0) {
            return elapsedMs;
        }
        return static_cast<uint32_t>(
            (static_cast<uint64_t>(elapsedMs) * resolvedCycles) % durationMs);
    }

    [[nodiscard]] static constexpr uint32_t modulatedRingsQ8(uint8_t rings,
                                                             uint8_t scale) {
        return (static_cast<uint32_t>(rings) * scale * q8Unit) / fullScale;
    }

    [[nodiscard]] static uint32_t leadingEdgeDistanceQ8(uint32_t elapsedMs,
                                                        uint32_t durationMs,
                                                        uint32_t travelQ8) {
        const auto scaled =
            (static_cast<uint64_t>(elapsedMs) * travelQ8) / durationMs;
        return static_cast<uint32_t>(
            std::min<uint64_t>(scaled, std::numeric_limits<uint32_t>::max()));
    }

    [[nodiscard]] static constexpr uint8_t starPointScale(uint8_t theta,
                                                          uint8_t radial,
                                                          uint8_t points,
                                                          uint8_t twist) {
        const auto phase =
            static_cast<uint8_t>((static_cast<uint16_t>(theta) * points) +
                                 (static_cast<uint16_t>(radial) * twist));
        return Primitives::FieldMath::triangle8(phase);
    }

    [[nodiscard]] static constexpr uint8_t
    pointBrightnessScale(uint8_t pointScale) {
        constexpr uint8_t pointFloor = 32;
        if (pointScale <= pointFloor) {
            return 0;
        }
        const auto expanded = static_cast<uint8_t>(
            ((static_cast<uint16_t>(pointScale - pointFloor) * fullScale) /
             (fullScale - pointFloor)));
        return Primitives::FieldMath::smoothstep8(expanded);
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
