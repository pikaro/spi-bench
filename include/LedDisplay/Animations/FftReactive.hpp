#pragma once

#include "Audio/Interfaces/Wire.hpp"
#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#include "Macros/Facade.hpp"
#include "Macros/internal/Markers.hpp"
#include "Types/Error.hpp"
#include <cstdint>
#include <expected>
#include <limits>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG FftReactiveConfig {
    uint8_t baseHue = 0;
    uint8_t saturation = 255;
    uint8_t valueScale = 128;
};

struct FftReactive {
    static constexpr AnimationKind kind = AnimationKind::FftReactive;
    static constexpr Layer defaultLayer = Layer::Fft;
    static constexpr uint16_t defaultLifetimeMs = 0;
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

    static std::expected<AnimationCommand, ReturnCode>
    makeCommand(FftReactiveConfig commandConfig = {},
                uint16_t requestId = 0,
                uint16_t lifetimeMs = defaultLifetimeMs,
                Layer layer = defaultLayer);

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
            ctx.canvas.ring(
                radial,
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
                                 animatedHue +
                                 spoke * fallbackSpokeHueStride +
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

static_assert(std::is_trivially_copyable_v<FftReactiveConfig>,
              "FftReactiveConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
