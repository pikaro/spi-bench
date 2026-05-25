#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG WheelIndicatorConfig {
    uint8_t hue = 160;
    uint8_t saturation = 255;
    uint8_t value = 96;
    uint8_t spokes = 3;
    uint8_t falloff = 1;
};

struct WheelIndicatorSpec {
    static constexpr AnimationKind kind = AnimationKind::WheelIndicator;
    static constexpr Layer defaultLayer = Layer::Wheel;
    static constexpr uint16_t defaultLifetimeMs = 0;
    static constexpr uint16_t defaultRequestId = 1;
    static constexpr uint8_t minimumSpokes = 1;
};

static_assert(std::is_trivially_copyable_v<WheelIndicatorConfig>,
              "WheelIndicatorConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
