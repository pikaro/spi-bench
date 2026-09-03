#pragma once

#include "LedDisplay/Animations/RadialGauge/Config.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct RadialGauge {
    static constexpr AnimationKind kind = RadialGaugeSpec::kind;
    static constexpr Layer defaultLayer = RadialGaugeSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        RadialGaugeSpec::defaultLifetimeMs;
    static constexpr uint16_t defaultRequestId =
        RadialGaugeSpec::defaultRequestId;
    static constexpr bool requiresFullFrame = false;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::Replace,
                                                 .opacity = 255};

    RadialGaugeConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto filledSpokes = filledSpokeCount(config);
        if (filledSpokes == 0) {
            return;
        }
        const auto band = ringBand(config);
        for (uint8_t spoke = 0; spoke < filledSpokes; ++spoke) {
            const auto color = colorForSpoke(config, spoke);
            for (uint8_t offset = 0; offset < band.width; ++offset) {
                ctx.canvas.pixel(spoke,
                                 static_cast<uint8_t>(band.first + offset),
                                 color, BlendOp::Replace);
            }
        }
    }

    [[nodiscard]] static constexpr uint8_t
    filledSpokeCount(const RadialGaugeConfig &gauge) {
        if (gauge.maximumValue == 0 || Config::spokeCount == 0) {
            return 0;
        }
        const auto boundedValue =
            std::min<uint16_t>(gauge.value, gauge.maximumValue);
        const auto additionalSpokes = static_cast<uint32_t>(boundedValue) *
                                      (Config::spokeCount - 1U) /
                                      gauge.maximumValue;
        return static_cast<uint8_t>(1U + additionalSpokes);
    }

    [[nodiscard]] static constexpr HsvColor
    colorForSpoke(const RadialGaugeConfig &gauge, uint8_t spoke) {
        const auto fraction = Config::spokeCount <= 1
                                  ? 0U
                                  : (static_cast<uint32_t>(spoke) * 255U) /
                                        (Config::spokeCount - 1U);
        return HsvColor{
            .hue = lerpHue(gauge.startHue, gauge.endHue,
                           static_cast<uint8_t>(fraction)),
            .saturation = Primitives::FieldMath::lerp8(
                gauge.startSaturation, gauge.endSaturation,
                static_cast<uint8_t>(fraction)),
            .value =
                Primitives::FieldMath::lerp8(gauge.startValue, gauge.endValue,
                                             static_cast<uint8_t>(fraction)),
        };
    }

  private:
    struct Band {
        uint8_t first = 0;
        uint8_t width = 1;
    };

    [[nodiscard]] static constexpr Band
    ringBand(const RadialGaugeConfig &gauge) {
        const auto width =
            std::clamp<uint8_t>(gauge.ringWidth, 1, Config::ringCount);
        const auto center =
            gauge.centerRing == RadialGaugeSpec::outermostRing
                ? static_cast<uint8_t>(Config::ringCount - 1U)
                : std::min<uint8_t>(
                      gauge.centerRing,
                      static_cast<uint8_t>(Config::ringCount - 1U));

        int16_t first = static_cast<int16_t>(center) -
                        static_cast<int16_t>((width - 1U) / 2U);
        if (first < 0) {
            first = 0;
        }
        if (static_cast<uint16_t>(first) + width > Config::ringCount) {
            first = static_cast<int16_t>(Config::ringCount - width);
        }
        return Band{.first = static_cast<uint8_t>(first), .width = width};
    }

    [[nodiscard]] static constexpr uint8_t lerpHue(uint8_t start, uint8_t end,
                                                   uint8_t fraction) {
        auto delta = static_cast<int16_t>(end) - static_cast<int16_t>(start);
        if (delta > 127) {
            delta -= 256;
        } else if (delta < -128) {
            delta += 256;
        }
        const auto interpolated =
            static_cast<int16_t>(start) + ((delta * fraction) / 255);
        return static_cast<uint8_t>(interpolated);
    }
};

} // namespace Totem::LedDisplay::Animations
