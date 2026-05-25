#pragma once

#include "LedDisplay/Animations/DiagnosticFill/Config.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct DiagnosticFill {
    static constexpr AnimationKind kind = DiagnosticFillSpec::kind;
    static constexpr Layer defaultLayer = DiagnosticFillSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        DiagnosticFillSpec::defaultLifetimeMs;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::Replace,
                                                 .opacity = 255};

    DiagnosticFillConfig config{};

    void render(AnimationRenderContext &ctx) const {
        ctx.canvas.fill(
            HsvColor{.hue = static_cast<uint8_t>(config.hue + ctx.hueOffset),
                     .saturation = config.saturation,
                     .value = config.value});
    }
};

} // namespace Totem::LedDisplay::Animations
