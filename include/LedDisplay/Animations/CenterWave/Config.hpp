#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG CenterWaveConfig {
    uint8_t hue = 144;
    uint8_t saturation = 255;
    uint8_t value = 180;
    uint8_t rise = 2;
    uint8_t peak = 1;
    uint8_t wake = 5;
};

struct CenterWaveSpec {
    static constexpr AnimationKind kind = AnimationKind::CenterWave;
    static constexpr Layer defaultLayer = Layer::Effect;
    static constexpr uint16_t defaultLifetimeMs = 1200;
    static constexpr uint8_t minimumPeakRings = 1;
};

static_assert(std::is_trivially_copyable_v<CenterWaveConfig>,
              "CenterWaveConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
