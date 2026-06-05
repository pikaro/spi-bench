#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG CymaticConfig {
    uint8_t hue = 176;
    uint8_t saturation = 240;
    uint8_t value = 180;
    uint8_t sourceMode = 0;
    uint8_t wavelength = 36;
    uint8_t speed = 96;
    uint8_t contrast = 180;
    uint8_t hueStep = 16;
};

struct CymaticSpec {
    static constexpr AnimationKind kind = AnimationKind::Cymatic;
    static constexpr Layer defaultLayer = Layer::Effect;
    static constexpr uint16_t defaultLifetimeMs = 3200;
    static constexpr uint8_t minimumWavelength = 1;
};

static_assert(std::is_trivially_copyable_v<CymaticConfig>,
              "CymaticConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
