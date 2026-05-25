#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG FftReactiveConfig {
    uint8_t baseHue = 0;
    uint8_t saturation = 255;
    uint8_t valueScale = 128;
};

struct FftReactiveSpec {
    static constexpr AnimationKind kind = AnimationKind::FftReactive;
    static constexpr Layer defaultLayer = Layer::Fft;
    static constexpr uint16_t defaultLifetimeMs = 0;
};

static_assert(std::is_trivially_copyable_v<FftReactiveConfig>,
              "FftReactiveConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
