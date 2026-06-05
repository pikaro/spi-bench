#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG BoltConfig {
    uint8_t hue = 24;
    uint8_t saturation = 255;
    uint8_t value = 255;
    uint8_t width = 1;
    uint8_t jitter = 1;
    uint8_t forks = 1;
    uint8_t seed = 0;
    bool outerOrigin = true;
};

struct BoltSpec {
    static constexpr AnimationKind kind = AnimationKind::Bolt;
    static constexpr Layer defaultLayer = Layer::Effect;
    static constexpr uint16_t defaultLifetimeMs = 900;
    static constexpr uint8_t minimumWidth = 1;
};

static_assert(std::is_trivially_copyable_v<BoltConfig>,
              "BoltConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
