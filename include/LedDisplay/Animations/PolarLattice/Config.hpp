#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG PolarLatticeConfig {
    uint8_t hue = 64;
    uint8_t saturation = 255;
    uint8_t value = 170;
    uint8_t radialMode = 4;
    uint8_t angularMode = 3;
    uint8_t speed = 96;
    uint8_t mix = 128;
    uint8_t contrast = 160;
};

struct PolarLatticeSpec {
    static constexpr AnimationKind kind = AnimationKind::PolarLattice;
    static constexpr Layer defaultLayer = Layer::TransientEffect;
    static constexpr uint16_t defaultLifetimeMs = 2400;
};

static_assert(std::is_trivially_copyable_v<PolarLatticeConfig>,
              "PolarLatticeConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
