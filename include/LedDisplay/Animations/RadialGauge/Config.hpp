#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG RadialGaugeConfig {
    uint16_t value = 16;
    uint16_t maximumValue = 31;

    uint8_t startHue = 0;
    uint8_t startSaturation = 0;
    uint8_t startValue = 255;
    uint8_t endHue = 0;
    uint8_t endSaturation = 0;
    uint8_t endValue = 255;

    // 255 resolves to the topology's outermost ring on the GPU.
    uint8_t centerRing = 255;
    uint8_t ringWidth = 1;
};

struct RadialGaugeSpec {
    static constexpr AnimationKind kind = AnimationKind::RadialGauge;
    static constexpr Layer defaultLayer = Layer::UI;
    static constexpr uint16_t defaultLifetimeMs = 500;
    static constexpr uint16_t defaultRequestId = 5;
    static constexpr uint8_t outermostRing = 255;
};

static_assert(std::is_trivially_copyable_v<RadialGaugeConfig>,
              "RadialGaugeConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
