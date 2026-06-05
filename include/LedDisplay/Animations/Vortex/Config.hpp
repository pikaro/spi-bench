#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG VortexConfig {
    uint8_t hue = 160;
    uint8_t saturation = 255;
    uint8_t value = 220;
    uint8_t arms = 3;
    uint8_t twist = 5;
    uint8_t width = 128;
    uint8_t cycles = 1;
    uint8_t hueStep = 24;
};

struct VortexSpec {
    static constexpr AnimationKind kind = AnimationKind::Vortex;
    static constexpr Layer defaultLayer = Layer::Effect;
    static constexpr uint16_t defaultLifetimeMs = 2400;
    static constexpr uint8_t minimumArms = 1;
    static constexpr uint8_t minimumWidth = 1;
    static constexpr uint8_t minimumCycles = 1;
};

static_assert(std::is_trivially_copyable_v<VortexConfig>,
              "VortexConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
