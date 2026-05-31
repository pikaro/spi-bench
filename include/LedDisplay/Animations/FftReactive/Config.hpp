#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG FftReactiveConfig {
    uint8_t baseHue = 144;
    uint8_t saturation = 255;
    uint8_t value = 220;
    uint8_t baseValue = 20;
    uint8_t radialMode = 2;
    uint8_t angularMode = 3;
    uint8_t symmetry = 4;
    uint8_t contrast = 180;
    uint8_t peakSensitivity = 96;
    uint8_t flowSpeed = 40;
};

struct FftReactiveSpec {
    static constexpr AnimationKind kind = AnimationKind::FftReactive;
    static constexpr Layer defaultLayer = Layer::Fft;
    static constexpr uint16_t defaultLifetimeMs = 0;
};

static_assert(std::is_trivially_copyable_v<FftReactiveConfig>,
              "FftReactiveConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
