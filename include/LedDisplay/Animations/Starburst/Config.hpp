#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG StarburstConfig {
    uint8_t hue = 32;
    uint8_t saturation = 255;
    uint8_t value = 220;
    uint8_t rise = 1;
    uint8_t peak = 2;
    uint8_t wake = 6;
    uint8_t points = 4;
    uint8_t pointGain = 2;
    uint8_t twist = 0;
    uint8_t cycles = 1;
};

struct StarburstSpec {
    static constexpr AnimationKind kind = AnimationKind::Starburst;
    static constexpr Layer defaultLayer = Layer::TransientEffect;
    static constexpr uint16_t defaultLifetimeMs = 1200;
    static constexpr uint8_t minimumPeakRings = 1;
    static constexpr uint8_t minimumPoints = 1;
    static constexpr uint8_t minimumCycles = 1;
};

static_assert(std::is_trivially_copyable_v<StarburstConfig>,
              "StarburstConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
