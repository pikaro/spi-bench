#pragma once

#include "Audio/Interfaces/Wire.hpp"
#include "LedDisplay/Animations/FftReactive/Config.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#include <cstdint>
#include <limits>

namespace Totem::LedDisplay::Animations {

struct FftReactive {
    static constexpr AnimationKind kind = FftReactiveSpec::kind;
    static constexpr Layer defaultLayer = FftReactiveSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        FftReactiveSpec::defaultLifetimeMs;
    static constexpr uint32_t fallbackHueFramePeriodMs = 12;
    static constexpr uint8_t fallbackSpokeHueStride = 16;
    static constexpr uint8_t fallbackRadialHueStride = 3;
    static constexpr uint8_t fftBandHueStride = 24;
    static constexpr uint16_t maxWireBandByteValue =
        std::numeric_limits<uint8_t>::max();
    static constexpr uint8_t wireBandHighByteShift = 8;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    FftReactiveConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto baseHue =
            static_cast<uint8_t>(config.baseHue + ctx.hueOffset);
        if (!ctx.inputs.hasFftFrame) {
            renderFallbackGradient(ctx, baseHue);
            return;
        }

        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            const size_t band =
                (static_cast<size_t>(radial) * Totem::Audio::fftBandCount) /
                Config::ringCount;
            const uint8_t bandValue = Renderers::GenericRenderer::scale8(
                fftBandValue(ctx.inputs.fftFrame, band), config.valueScale);
            ctx.canvas.ring(radial,
                            HsvColor{.hue = static_cast<uint8_t>(
                                         baseHue + band * fftBandHueStride),
                                     .saturation = config.saturation,
                                     .value = bandValue});
        }
    }

  private:
    void renderFallbackGradient(AnimationRenderContext &ctx,
                                uint8_t baseHue) const {
        const uint8_t animatedHue = static_cast<uint8_t>(
            baseHue + (ctx.clock.nowMs / fallbackHueFramePeriodMs));
        for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
            for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
                ctx.canvas.pixel(
                    spoke, radial,
                    HsvColor{.hue = static_cast<uint8_t>(
                                 animatedHue + spoke * fallbackSpokeHueStride +
                                 radial * fallbackRadialHueStride),
                             .saturation = config.saturation,
                             .value = config.valueScale});
            }
        }
    }

    [[nodiscard]] static uint8_t
    fftBandValue(const Totem::Audio::FftFrame &frame, size_t band) {
        uint16_t raw = 0;
        switch (static_cast<Totem::Audio::FftBand>(band)) {
        case Totem::Audio::FftBand::SubBass:
            raw = frame.subBass;
            break;
        case Totem::Audio::FftBand::Bass:
            raw = frame.bass;
            break;
        case Totem::Audio::FftBand::LowMid:
            raw = frame.lowMid;
            break;
        case Totem::Audio::FftBand::Mid:
            raw = frame.mid;
            break;
        case Totem::Audio::FftBand::HighMid:
            raw = frame.highMid;
            break;
        case Totem::Audio::FftBand::Presence:
            raw = frame.presence;
            break;
        case Totem::Audio::FftBand::Brilliance:
            raw = frame.brilliance;
            break;
        case Totem::Audio::FftBand::Air:
        default:
            raw = frame.air;
            break;
        }
        if (raw <= maxWireBandByteValue) {
            return static_cast<uint8_t>(raw);
        }
        return static_cast<uint8_t>(raw >> wireBandHighByteShift);
    }
};

} // namespace Totem::LedDisplay::Animations
