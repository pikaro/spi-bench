#pragma once

#include "LedDisplay/Interfaces/Blend.hpp"
#include "LedDisplay/Interfaces/Color.hpp"
#include <cstdint>

namespace Totem::LedDisplay::detail {

template <class Impl> struct Renderer {
    [[nodiscard]] static uint8_t scale8(uint8_t value, uint8_t scale) {
        return Impl::scale8(value, scale);
    }

    [[nodiscard]] static uint8_t qadd8(uint8_t a, uint8_t b) {
        return Impl::qadd8(a, b);
    }

    [[nodiscard]] static uint8_t sine8(uint8_t phase) {
        return Impl::sine8(phase);
    }

    [[nodiscard]] static HsvColor blend(HsvColor dst, HsvColor src,
                                        BlendOp op) {
        return Impl::blend(dst, src, op);
    }

    [[nodiscard]] static RgbColor hsvToRgb(HsvColor hsv) {
        return Impl::hsvToRgb(hsv);
    }
};

} // namespace Totem::LedDisplay::detail
