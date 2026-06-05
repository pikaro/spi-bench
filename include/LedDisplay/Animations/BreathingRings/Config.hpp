#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG BreathingRingsConfig {
    uint8_t hue = 112;
    uint8_t saturation = 255;
    uint8_t value = 170;
    uint8_t spacing = 8;
    uint8_t width = 3;
    uint8_t cycles = 1;
    uint8_t direction = 0;
    uint8_t hueStep = 8;
};

struct BreathingRingsSpec {
    static constexpr AnimationKind kind = AnimationKind::BreathingRings;
    static constexpr Layer defaultLayer = Layer::TransientEffect;
    static constexpr uint16_t defaultLifetimeMs = 2400;
    static constexpr uint8_t minimumSpacing = 2;
    static constexpr uint8_t minimumWidth = 1;
    static constexpr uint8_t minimumCycles = 1;
};

static_assert(std::is_trivially_copyable_v<BreathingRingsConfig>,
              "BreathingRingsConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
