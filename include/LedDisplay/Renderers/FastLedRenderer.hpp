#pragma once

#include "LedDisplay/Interfaces/Blend.hpp"
#include "LedDisplay/Interfaces/Color.hpp"
#include "LedDisplay/detail/FastLedCompat.hpp"
#include <FastLED.h>

namespace Totem::LedDisplay::Renderers {

struct FastLedRenderer {
    [[nodiscard]] static uint8_t scale8(uint8_t value, uint8_t scale) {
        return ::fl::scale8(value, scale);
    }

    [[nodiscard]] static uint8_t qadd8(uint8_t a, uint8_t b) {
        return ::fl::qadd8(a, b);
    }

    [[nodiscard]] static HsvColor blend(HsvColor dst, HsvColor src,
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
            return src.value > dst.value ? src : dst;
        }
    }

    [[nodiscard]] static RgbColor hsvToRgb(HsvColor hsv) {
        const auto rgb = ::fl::CHSV(hsv.hue, hsv.saturation, hsv.value);
        ::fl::CRGB out{};
        ::fl::hsv2rgb_rainbow(rgb, out);
        return RgbColor{.red = out.r, .green = out.g, .blue = out.b};
    }
};

} // namespace Totem::LedDisplay::Renderers
