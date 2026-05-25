#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG DiagnosticFillConfig {
    uint8_t hue = 0;
    uint8_t saturation = 0;
    uint8_t value = 48;
};

struct DiagnosticFillSpec {
    static constexpr AnimationKind kind = AnimationKind::DiagnosticFill;
    static constexpr Layer defaultLayer = Layer::Debug;
    static constexpr uint16_t defaultLifetimeMs = 2000;
};

static_assert(std::is_trivially_copyable_v<DiagnosticFillConfig>,
              "DiagnosticFillConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
