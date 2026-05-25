#pragma once

#include "LedDisplay/Animations/SineWave/Config.hpp"
#include "LedDisplay/Animations/detail/WaveGeometry.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#include "LedDisplay/Renderers/Waveform.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace Totem::LedDisplay::Animations {

struct SineWave {
    static constexpr AnimationKind kind = SineWaveSpec::kind;
    static constexpr Layer defaultLayer = SineWaveSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        SineWaveSpec::projectedLifetimeMs(SineWaveConfig{});
    static constexpr uint16_t defaultDurationMs =
        SineWaveSpec::defaultDurationMs;
    static constexpr uint16_t minimumDurationMs =
        SineWaveSpec::minimumDurationMs;
    static constexpr uint8_t minimumWidth = SineWaveSpec::minimumWidth;
    static constexpr uint8_t minimumWavelength =
        SineWaveSpec::minimumWavelength;
    static constexpr uint8_t spokeGainPhaseStep =
        SineWaveSpec::spokeGainPhaseStep;
    static constexpr uint8_t defaultTailDecay = SineWaveSpec::defaultTailDecay;
    static constexpr uint8_t defaultPeakHold = SineWaveSpec::defaultPeakHold;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    SineWaveConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto duration =
            std::max<uint16_t>(config.durationMs, minimumDurationMs);
        const auto maxDistance =
            detail::maxTravelDistanceQ8(config.travelRings);
        const auto scanFront =
            scanFrontDistanceQ8(ctx.clock.elapsedMs, duration, maxDistance);
        const auto headDistance =
            static_cast<uint16_t>(std::min<uint32_t>(scanFront, maxDistance));
        const auto head =
            detail::originRadialQ8(headDistance, config.outerOrigin);
        const auto renderHead = scanFront <= maxDistance;
        const auto radius =
            static_cast<uint16_t>(std::max(config.width, minimumWidth)) *
            detail::q8Unit;
        const auto headValue = stampedValue(headDistance, config);
        const auto hue = static_cast<uint8_t>(config.hue + ctx.hueOffset);

        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            const auto center = static_cast<uint16_t>(radial) * detail::q8Unit;
            const auto originDistance =
                detail::originDistanceQ8(center, config.outerOrigin);
            if (originDistance > maxDistance) {
                continue;
            }

            auto value = tracedValue(originDistance, scanFront, config);
            if (renderHead) {
                const auto headDistanceFromCenter =
                    detail::distanceQ8(center, head);
                const auto headScale =
                    detail::lobeScale(headDistanceFromCenter, radius);
                if (headScale != 0) {
                    value = std::max(value, Renderers::GenericRenderer::scale8(
                                                headValue, headScale));
                }
            }
            if (value == 0) {
                continue;
            }
            for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
                const auto gained = detail::applySpokeGain(
                    value, spoke, config.spokeGainPct, spokeGainPhaseStep);
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
    [[nodiscard]] static uint32_t scanFrontDistanceQ8(uint32_t elapsedMs,
                                                      uint16_t durationMs,
                                                      uint16_t maxDistanceQ8) {
        if (maxDistanceQ8 == 0) {
            return 0;
        }
        const auto scaled =
            (static_cast<uint64_t>(elapsedMs) * maxDistanceQ8) / durationMs;
        return static_cast<uint32_t>(
            std::min<uint64_t>(scaled, std::numeric_limits<uint32_t>::max()));
    }

    [[nodiscard]] static uint8_t sineScale(uint16_t distanceFromOriginQ8,
                                           uint8_t wavelengthRings) {
        const auto wavelength =
            std::max<uint8_t>(wavelengthRings, minimumWavelength);
        const auto cycleQ8 = static_cast<uint32_t>(wavelength) * detail::q8Unit;
        const auto phase = static_cast<uint8_t>(
            192U +
            ((static_cast<uint32_t>(distanceFromOriginQ8) * 256U) / cycleQ8));
        return Renderers::Waveform::sine8(phase);
    }

    [[nodiscard]] static uint8_t stampedValue(uint16_t distanceFromOriginQ8,
                                              const SineWaveConfig &config) {
        return detail::blendValue(
            config.baseValue, config.value,
            sineScale(distanceFromOriginQ8, config.wavelength));
    }

    [[nodiscard]] static uint8_t tracedValue(uint16_t distanceFromOriginQ8,
                                             uint32_t scanFrontQ8,
                                             const SineWaveConfig &config) {
        if (distanceFromOriginQ8 > scanFrontQ8) {
            return 0;
        }
        const auto waveScale =
            sineScale(distanceFromOriginQ8, config.wavelength);
        const auto value =
            detail::blendValue(config.baseValue, config.value, waveScale);
        return applyTailDecay(
            value, waveScale,
            static_cast<uint32_t>(scanFrontQ8 - distanceFromOriginQ8),
            config.tailDecay, config.peakHold);
    }

    [[nodiscard]] static uint8_t
    applyTailDecay(uint8_t value, uint8_t waveScale, uint32_t ageQ8,
                   uint8_t tailDecay, uint8_t peakHold) {
        if (value == 0 || tailDecay == 0 || ageQ8 == 0) {
            return value;
        }

        auto decay =
            (static_cast<uint64_t>(ageQ8) * tailDecay) / detail::q8Unit;
        if (peakHold != 0) {
            const auto retained =
                (static_cast<uint32_t>(waveScale) * peakHold) /
                detail::fullScale;
            decay =
                (decay * (detail::fullScale - retained)) / detail::fullScale;
        }
        if (decay >= value) {
            return 0;
        }
        return static_cast<uint8_t>(value - decay);
    }
};

} // namespace Totem::LedDisplay::Animations
