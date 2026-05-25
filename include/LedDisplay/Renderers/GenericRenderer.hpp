#pragma once

#include "LedDisplay/Interfaces/Blend.hpp"
#include "LedDisplay/Interfaces/Color.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace Totem::LedDisplay::Renderers {

struct GenericRenderer {
    static constexpr uint8_t channelMax = std::numeric_limits<uint8_t>::max();
    static constexpr uint16_t hueStepCount =
        static_cast<uint16_t>(channelMax) + 1U;
    static constexpr uint8_t hsvRegionCount = 6U;
    static constexpr uint8_t hsvRegionCeilDivBias = hsvRegionCount - 1U;
    static constexpr uint8_t hsvRegionWidth = static_cast<uint8_t>(
        (hueStepCount + hsvRegionCeilDivBias) / hsvRegionCount);
    static constexpr uint8_t hsvRegionScale = hsvRegionCount;

    [[nodiscard]] static constexpr uint8_t scale8(uint8_t value,
                                                  uint8_t scale) {
        return static_cast<uint8_t>((static_cast<uint16_t>(value) * scale) /
                                    channelMax);
    }

    [[nodiscard]] static constexpr uint8_t qadd8(uint8_t a, uint8_t b) {
        const auto sum = static_cast<uint16_t>(a) + b;
        return static_cast<uint8_t>(std::min<uint16_t>(sum, channelMax));
    }

    [[nodiscard]] static constexpr uint8_t sine8(uint8_t phase) {
        const auto offset =
            (phase & 0x40U) != 0 ? static_cast<uint8_t>(255U - phase) : phase;
        const auto sectionOffset = static_cast<uint8_t>(offset & 0x3FU);
        uint8_t secondaryOffset = static_cast<uint8_t>(sectionOffset & 0x0FU);
        if ((phase & 0x40U) != 0) {
            ++secondaryOffset;
        }

        const auto section = static_cast<uint8_t>(sectionOffset >> 4U);
        const auto base = sinBase(section);
        const auto slope = sinSlope(section);
        auto y = static_cast<int16_t>(
            base + static_cast<uint8_t>((slope * secondaryOffset) >> 4U));
        if ((phase & 0x80U) != 0) {
            y = -y;
        }
        return static_cast<uint8_t>(y + 128);
    }

    [[nodiscard]] static constexpr HsvColor blend(HsvColor dst, HsvColor src,
                                                  BlendOp op) {
        switch (op) {
        case BlendOp::Replace:
            return src;
        case BlendOp::AddValue:
            if (src.value > dst.value) {
                dst.hue = src.hue;
                dst.saturation = src.saturation;
            }
            dst.value = qadd8(dst.value, src.value);
            return dst;
        case BlendOp::Alpha:
            return src;
        case BlendOp::MaxValue:
        default:
            return src.value >= dst.value ? src : dst;
        }
    }

    [[nodiscard]] static constexpr RgbColor hsvToRgb(HsvColor hsv) {
        if (hsv.saturation == 0) {
            return RgbColor{
                .red = hsv.value, .green = hsv.value, .blue = hsv.value};
        }

        const uint8_t region = hsv.hue / hsvRegionWidth;
        const uint8_t remainder = static_cast<uint8_t>(
            (hsv.hue - (region * hsvRegionWidth)) * hsvRegionScale);
        const uint8_t p = scale8(hsv.value, channelMax - hsv.saturation);
        const uint8_t q =
            scale8(hsv.value, channelMax - scale8(hsv.saturation, remainder));
        const uint8_t t = scale8(
            hsv.value,
            channelMax - scale8(hsv.saturation,
                                static_cast<uint8_t>(channelMax - remainder)));

        switch (region) {
        case 0:
            return RgbColor{.red = hsv.value, .green = t, .blue = p};
        case 1:
            return RgbColor{.red = q, .green = hsv.value, .blue = p};
        case 2:
            return RgbColor{.red = p, .green = hsv.value, .blue = t};
        case 3:
            return RgbColor{.red = p, .green = q, .blue = hsv.value};
        case 4:
            return RgbColor{.red = t, .green = p, .blue = hsv.value};
        default:
            return RgbColor{.red = hsv.value, .green = p, .blue = q};
        }
    }

  private:
    [[nodiscard]] static constexpr uint8_t sinBase(uint8_t section) {
        switch (section) {
        case 0:
            return 0;
        case 1:
            return 49;
        case 2:
            return 90;
        case 3:
        default:
            return 117;
        }
    }

    [[nodiscard]] static constexpr uint8_t sinSlope(uint8_t section) {
        switch (section) {
        case 0:
            return 49;
        case 1:
            return 41;
        case 2:
            return 27;
        case 3:
        default:
            return 10;
        }
    }
};

} // namespace Totem::LedDisplay::Renderers
