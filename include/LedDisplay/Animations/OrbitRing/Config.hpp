#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG OrbitRingConfig {
    uint8_t hue = 96;
    uint8_t saturation = 255;
    uint8_t value = 220;
    uint8_t radius = 128;
    uint8_t radialWidth = 36;
    uint8_t angularWidth = 28;
    uint8_t comets = 2;
    uint8_t laps = 1;
    uint8_t trail = 24;
};

struct OrbitRingSpec {
    static constexpr AnimationKind kind = AnimationKind::OrbitRing;
    static constexpr Layer defaultLayer = Layer::Effect;
    static constexpr uint16_t defaultLifetimeMs = 2400;
    static constexpr uint8_t minimumWidth = 1;
    static constexpr uint8_t minimumComets = 1;
    static constexpr uint8_t minimumLaps = 1;
};

static_assert(std::is_trivially_copyable_v<OrbitRingConfig>,
              "OrbitRingConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
