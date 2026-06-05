#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG LighthouseConfig {
    uint8_t hue = 144;
    uint8_t saturation = 255;
    uint8_t value = 220;
    uint8_t beamWidth = 3;
    uint8_t trailSpokes = 4;
    uint8_t cycles = 1;
    uint8_t innerRing = 0;
    uint8_t outerRing = 0;
};

struct LighthouseSpec {
    static constexpr AnimationKind kind = AnimationKind::Lighthouse;
    static constexpr Layer defaultLayer = Layer::Effect;
    static constexpr uint16_t defaultLifetimeMs = 3000;
    static constexpr uint8_t minimumBeamWidth = 3;
    static constexpr uint8_t minimumCycles = 1;
};

static_assert(std::is_trivially_copyable_v<LighthouseConfig>,
              "LighthouseConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
