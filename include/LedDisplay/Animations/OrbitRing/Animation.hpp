#pragma once

#include "LedDisplay/Animations/OrbitRing/Config.hpp"
#include "LedDisplay/Animations/detail/SimpleField.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct OrbitRing {
    static constexpr AnimationKind kind = OrbitRingSpec::kind;
    static constexpr Layer defaultLayer = OrbitRingSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        OrbitRingSpec::defaultLifetimeMs;
    static constexpr bool requiresFullFrame = false;
    static constexpr uint8_t minimumWidth = OrbitRingSpec::minimumWidth;
    static constexpr uint8_t minimumComets = OrbitRingSpec::minimumComets;
    static constexpr uint8_t minimumLaps = OrbitRingSpec::minimumLaps;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    OrbitRingConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto duration =
            detail::nonzero(ctx.clock.durationMs, defaultLifetimeMs);
        const auto progress =
            detail::progress8(ctx.clock.elapsedMs, duration, config.laps);
        const auto radialProgress =
            detail::progress8(ctx.clock.elapsedMs, duration);
        const auto radialCenter =
            driftedRadius(config.radius, config.radialDrift,
                          config.radialDirection, radialProgress);
        const auto radialWidth =
            std::max<uint8_t>(config.radialWidth, minimumWidth);
        const auto angularWidth =
            std::max<uint8_t>(config.angularWidth, minimumWidth);
        const auto comets = std::max<uint8_t>(config.comets, minimumComets);
        const auto hue = static_cast<uint8_t>(config.hue + ctx.hueOffset);
        const auto sparklePhase =
            static_cast<uint8_t>(ctx.clock.elapsedMs / sparkleStepMs);

        Primitives::forEachLogicalPixel(
            [&](const Primitives::FieldPoint &point) {
                const auto radialScale = Primitives::FieldMath::ringPulse(
                    detail::stripRadius8(point), radialCenter, radialWidth);
                if (radialScale == 0) {
                    return;
                }

                CometSample best{};
                uint8_t sparkleSeed = 0;
                for (uint8_t comet = 0; comet < comets; ++comet) {
                    const auto center = static_cast<uint8_t>(
                        progress +
                        ((static_cast<uint16_t>(comet) * detail::simpleQ8Unit) /
                         comets));
                    const auto sample = cometScale(point.theta, center,
                                                   angularWidth, config.trail);
                    if (sample.scale > best.scale) {
                        best = sample;
                        sparkleSeed = comet;
                    }
                }
                if (best.scale == 0) {
                    return;
                }

                auto value =
                    detail::scale2(config.value, radialScale, best.scale);
                if (value == 0) {
                    return;
                }
                auto pixelHue = hue;
                if (best.trailScale != 0) {
                    applySparkle(point, sparklePhase, sparkleSeed, value,
                                 pixelHue);
                    if (value == 0) {
                        return;
                    }
                }
                ctx.canvas.pixel(point.spoke, point.radial,
                                 HsvColor{.hue = pixelHue,
                                          .saturation = config.saturation,
                                          .value = value});
            });
    }

  private:
    struct CometSample {
        uint8_t scale = 0;
        uint8_t trailScale = 0;
    };

    static constexpr uint16_t sparkleStepMs = 80;
    static constexpr uint8_t sparkleValueFloor = 64;

    [[nodiscard]] static constexpr uint8_t driftedRadius(uint8_t radius,
                                                         uint8_t drift,
                                                         uint8_t direction,
                                                         uint8_t progress) {
        if (drift == 0 || direction == OrbitRingSpec::radialFixed) {
            return radius;
        }
        const auto offset = Primitives::FieldMath::scale8(drift, progress);
        if (direction == OrbitRingSpec::radialInward) {
            return radius > offset ? static_cast<uint8_t>(radius - offset) : 0;
        }
        if (direction == OrbitRingSpec::radialOutward) {
            return static_cast<uint8_t>(
                std::min<uint16_t>(detail::simpleFullScale,
                                   static_cast<uint16_t>(radius) + offset));
        }
        return radius;
    }

    [[nodiscard]] static constexpr CometSample
    cometScale(uint8_t theta, uint8_t center, uint8_t width, uint8_t trail) {
        const auto head = detail::pulseByDistance8(
            Primitives::FieldMath::angularDistance(theta, center), width);
        if (trail == 0) {
            return CometSample{.scale = head, .trailScale = 0};
        }
        const auto behind = detail::directionalBehind(center, theta);
        const auto ahead = detail::directionalBehind(theta, center);
        const auto leadWidth = std::min<uint8_t>(width, spokeWidth());
        const auto leadScale = detail::pulseByDistance8(ahead, leadWidth);
        const auto envelopeWidth = std::min<uint16_t>(
            detail::simpleFullScale, static_cast<uint16_t>(width) + trail);
        if (behind >= envelopeWidth) {
            return CometSample{.scale = leadScale, .trailScale = 0};
        }
        const auto envelopeScale = static_cast<uint8_t>(
            detail::simpleFullScale -
            ((static_cast<uint16_t>(behind) * detail::simpleFullScale) /
             envelopeWidth));
        const auto trailScale =
            Primitives::FieldMath::smoothstep8(envelopeScale);
        return CometSample{
            .scale = std::max<uint8_t>(leadScale, trailScale),
            .trailScale = behind > width ? trailScale : uint8_t{0},
        };
    }

    [[nodiscard]] static constexpr uint8_t spokeWidth() {
        return static_cast<uint8_t>(
            std::max<uint16_t>(1U, detail::simpleQ8Unit / Config::spokeCount));
    }

    void applySparkle(const Primitives::FieldPoint &point, uint8_t sparklePhase,
                      uint8_t sparkleSeed, uint8_t &value, uint8_t &hue) const {
        const auto seed =
            static_cast<uint8_t>(sparklePhase + (sparkleSeed * 53U));
        const auto brightnessHash =
            Primitives::FieldMath::hash8(point.spoke, point.radial, seed);
        if (config.sparkle != 0) {
            const auto randomScale = static_cast<uint8_t>(
                sparkleValueFloor +
                Primitives::FieldMath::scale8(
                    brightnessHash,
                    static_cast<uint8_t>(detail::simpleFullScale -
                                         sparkleValueFloor)));
            const auto randomized =
                Primitives::FieldMath::scale8(value, randomScale);
            value =
                Primitives::FieldMath::lerp8(value, randomized, config.sparkle);
        }
        if (config.hueJitter != 0) {
            const auto hueHash = Primitives::FieldMath::hash8(
                point.radial, point.spoke, static_cast<uint8_t>(seed + 0x71U));
            hue = static_cast<uint8_t>(static_cast<int16_t>(hue) +
                                       signedJitter(hueHash, config.hueJitter));
        }
    }

    [[nodiscard]] static constexpr int16_t signedJitter(uint8_t hash,
                                                        uint8_t width) {
        const auto clampedWidth = std::min<uint8_t>(width, 127U);
        const auto range = static_cast<uint16_t>((clampedWidth * 2U) + 1U);
        return static_cast<int16_t>((static_cast<uint16_t>(hash) * range) /
                                    detail::simpleQ8Unit) -
               static_cast<int16_t>(clampedWidth);
    }
};

} // namespace Totem::LedDisplay::Animations
