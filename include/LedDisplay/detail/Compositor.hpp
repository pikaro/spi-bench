#pragma once

#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/Blend.hpp"
#include "LedDisplay/Interfaces/Color.hpp"
#include "LedDisplay/detail/RendererSelect.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace Totem::LedDisplay::detail {

struct BlendContext {
    std::span<const HsvColor> src;
    std::span<HsvColor> dst;
    AnimationStyle style{};
};

struct Compositor {
    static constexpr uint8_t fullOpacity =
        std::numeric_limits<uint8_t>::max();

    static void clear(std::span<HsvColor> frame) {
        for (auto &pixel : frame) {
            pixel = HsvColor{};
        }
    }

    static void decay(std::span<HsvColor> frame, uint8_t amount) {
        if (amount == 0) {
            return;
        }
        for (auto &pixel : frame) {
            pixel.value = pixel.value > amount
                              ? static_cast<uint8_t>(pixel.value - amount)
                              : 0;
        }
    }

    static void blend(BlendContext ctx) {
        const auto count = std::min(ctx.src.size(), ctx.dst.size());
        for (size_t i = 0; i < count; ++i) {
            blendPixel(ctx.dst[i], ctx.src[i], ctx.style);
        }
    }

  private:
    static void blendPixel(HsvColor &dst, HsvColor src,
                           AnimationStyle style) {
        const uint8_t value = Render::scale8(src.value, style.opacity);
        if (value == 0) {
            return;
        }
        src.value = value;

        switch (style.blendOp) {
        case BlendOp::Replace:
            dst = src;
            return;
        case BlendOp::AddValue: {
            const auto previous = dst.value;
            dst.value = Render::qadd8(dst.value, src.value);
            if (src.value > previous) {
                dst.hue = src.hue;
                dst.saturation = src.saturation;
            }
            return;
        }
        case BlendOp::Alpha: {
            const uint8_t inverse =
                static_cast<uint8_t>(fullOpacity - style.opacity);
            const auto retained = Render::scale8(dst.value, inverse);
            dst.value = Render::qadd8(retained, value);
            if (value > retained) {
                dst.hue = src.hue;
                dst.saturation = src.saturation;
            }
            return;
        }
        case BlendOp::MaxValue:
        default:
            if (src.value > dst.value) {
                dst = src;
            }
            return;
        }
    }
};

} // namespace Totem::LedDisplay::detail
