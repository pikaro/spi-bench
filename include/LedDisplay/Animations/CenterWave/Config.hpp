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
    uint8_t peak = 2;
    uint8_t wake = 5;
    uint8_t peakDelta = 0;
    uint8_t speedDelta = 0;
    uint8_t spokeModulo = 1;
};

struct CenterWaveSpec {
    static constexpr AnimationKind kind = AnimationKind::CenterWave;
    static constexpr Layer defaultLayer = Layer::TransientEffect;
    static constexpr uint16_t defaultLifetimeMs = 1200;
    static constexpr uint8_t minimumPeakRings = 1;
    static constexpr uint8_t minimumSpokeModulo = 1;
};

static_assert(std::is_trivially_copyable_v<CenterWaveConfig>,
              "CenterWaveConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
