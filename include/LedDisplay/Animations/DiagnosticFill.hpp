#pragma once

#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "Macros/Facade.hpp"
#include "Macros/internal/Markers.hpp"
#include "Types/Error.hpp"
#include <cstdint>
#include <expected>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG DiagnosticFillConfig {
    uint8_t hue = 0;
    uint8_t saturation = 0;
    uint8_t value = 48;
};

struct DiagnosticFill {
    static constexpr AnimationKind kind = AnimationKind::DiagnosticFill;
    static constexpr Layer defaultLayer = Layer::Debug;
    static constexpr uint16_t defaultLifetimeMs = 2000;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::Replace,
                                                 .opacity = 255};

    DiagnosticFillConfig config{};

    static std::expected<AnimationCommand, ReturnCode>
    makeCommand(DiagnosticFillConfig commandConfig = {},
                uint16_t requestId = 0,
                uint16_t lifetimeMs = defaultLifetimeMs,
                Layer layer = defaultLayer);

    void render(AnimationRenderContext &ctx) const {
        ctx.canvas.fill(
            HsvColor{.hue = static_cast<uint8_t>(config.hue + ctx.hueOffset),
                     .saturation = config.saturation,
                     .value = config.value});
    }
};

static_assert(std::is_trivially_copyable_v<DiagnosticFillConfig>,
              "DiagnosticFillConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
