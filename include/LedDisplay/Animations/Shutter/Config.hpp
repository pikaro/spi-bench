#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG ShutterConfig {
    uint8_t hue = 48;
    uint8_t saturation = 255;
    uint8_t value = 210;
    uint8_t segments = 8;
    uint8_t openPct = 128;
    uint8_t edgeWidth = 48;
    uint8_t rotationCycles = 1;
    uint8_t mode = 1;
};

struct ShutterSpec {
    static constexpr AnimationKind kind = AnimationKind::Shutter;
    static constexpr Layer defaultLayer = Layer::TransientEffect;
    static constexpr uint16_t defaultLifetimeMs = 1600;
    static constexpr uint8_t minimumSegments = 2;
    static constexpr uint8_t minimumEdgeWidth = 1;
};

static_assert(std::is_trivially_copyable_v<ShutterConfig>,
              "ShutterConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
