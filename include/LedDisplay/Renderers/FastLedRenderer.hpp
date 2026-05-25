#pragma once

#include "LedDisplay/Interfaces/Blend.hpp"
#include "LedDisplay/Interfaces/Color.hpp"
#include "LedDisplay/detail/FastLedCompat.hpp"
#include "StaticConfig/LedDisplay.hpp"
#include "Types/Basic.hpp"
#include <FastLED.h>
#include <cstdint>

namespace Totem::LedDisplay::Renderers {

template <HsvConversion> struct HsvToRgbConverter;

template <> struct HsvToRgbConverter<HsvConversion::Rainbow> {
    static void convert(const ::fl::CHSV &hsv, ::fl::CRGB &rgb) {
        ::fl::hsv2rgb_rainbow(hsv, rgb);
    }
};

template <> struct HsvToRgbConverter<HsvConversion::Spectrum> {
    static void convert(const ::fl::CHSV &hsv, ::fl::CRGB &rgb) {
        ::fl::hsv2rgb_spectrum(hsv, rgb);
    }
};

template <> struct HsvToRgbConverter<HsvConversion::Fullspectrum> {
    static void convert(const ::fl::CHSV &hsv, ::fl::CRGB &rgb) {
        ::fl::hsv2rgb_fullspectrum(hsv, rgb);
    }
};

struct FastLedRenderer {
    [[nodiscard]] static uint8_t scale8(uint8_t value, uint8_t scale) {
        return ::fl::scale8(value, scale);
    }

    [[nodiscard]] static uint8_t qadd8(uint8_t a, uint8_t b) {
        return ::fl::qadd8(a, b);
    }

    [[nodiscard]] static uint8_t sine8(uint8_t phase) { return ::sin8(phase); }

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
        HsvToRgbConverter<LedOutputStaticConfig::hsvConversion>::convert(rgb,
                                                                         out);
        return RgbColor{.red = out.r, .green = out.g, .blue = out.b};
    }
};

} // namespace Totem::LedDisplay::Renderers
