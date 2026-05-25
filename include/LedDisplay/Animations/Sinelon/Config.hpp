#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG SinelonConfig {
    uint8_t hue = 96;
    uint8_t saturation = 255;
    uint8_t value = 220;
    uint8_t width = 3;
    uint16_t periodMs = 2400;
    bool outerOrigin = false;
    uint8_t travelRings = 0;
    uint8_t bounceAttenuation = 255;
    uint16_t spokeGainPct = 100;
    uint8_t spokeGainPhaseStep = 64;
};

struct SinelonSpec {
    static constexpr AnimationKind kind = AnimationKind::Sinelon;
    static constexpr Layer defaultLayer = Layer::Effect;
    static constexpr uint16_t defaultLifetimeMs = 0;
    static constexpr uint16_t defaultPeriodMs = 2400;
    static constexpr uint16_t minimumPeriodMs = 100;
    static constexpr uint8_t minimumWidth = 1;
    static constexpr uint8_t defaultSpokeGainPhaseStep = 64;
};

static_assert(std::is_trivially_copyable_v<SinelonConfig>,
              "SinelonConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
