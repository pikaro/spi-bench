#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG OrbitSparksConfig {
    uint8_t baseHue = 32;
    uint8_t saturation = 255;
    uint8_t value = 230;
    uint8_t sparkCount = 32;
    uint8_t sparkSize = 1;
    uint8_t orbitSpeed = 32;
    uint8_t radialDrift = 96;
    uint8_t highSparkle = 160;
    uint8_t peakSensitivity = 128;
    uint8_t seed = 0xA5;
    uint8_t hueModulation = 160;
};

struct OrbitSparksSpec {
    static constexpr AnimationKind kind = AnimationKind::OrbitSparks;
    static constexpr Layer defaultLayer = Layer::Fft;
    static constexpr uint16_t defaultLifetimeMs = 0;
};

static_assert(std::is_trivially_copyable_v<OrbitSparksConfig>,
              "OrbitSparksConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
