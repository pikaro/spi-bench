#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG RadialCurtainConfig {
    uint8_t hue = 200;
    uint8_t saturation = 220;
    uint8_t value = 190;
    uint8_t width = 4;
    uint8_t tilt = 32;
    uint8_t speed = 128;
    bool outerOrigin = false;
    uint8_t spokePhase = 16;
};

struct RadialCurtainSpec {
    static constexpr AnimationKind kind = AnimationKind::RadialCurtain;
    static constexpr Layer defaultLayer = Layer::Effect;
    static constexpr uint16_t defaultLifetimeMs = 2600;
    static constexpr uint8_t minimumWidth = 1;
    static constexpr uint8_t minimumSpeed = 1;
};

static_assert(std::is_trivially_copyable_v<RadialCurtainConfig>,
              "RadialCurtainConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
