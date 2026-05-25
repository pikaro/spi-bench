#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG SpokeSweepConfig {
    uint8_t baseHue = 0;
    uint8_t hueStride = 16;
    uint8_t value = 220;
    uint8_t trailSpokes = 2;
    uint8_t cycles = 1;
    uint8_t markerValue = 255;
    bool useMarkers = true;
};

struct SpokeSweepSpec {
    static constexpr AnimationKind kind = AnimationKind::SpokeSweep;
    static constexpr Layer defaultLayer = Layer::Debug;
    static constexpr uint16_t defaultLifetimeMs = 6000;
    static constexpr uint16_t defaultRequestId = 2;
    static constexpr uint8_t minimumCycles = 1;
};

static_assert(std::is_trivially_copyable_v<SpokeSweepConfig>,
              "SpokeSweepConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
