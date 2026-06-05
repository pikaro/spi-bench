#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG SpectralIrisConfig {
    uint8_t baseHue = 96;
    uint8_t saturation = 255;
    uint8_t value = 220;
    uint8_t baseValue = 8;
    uint8_t petals = 8;
    uint8_t aperture = 128;
    uint8_t rimWidth = 28;
    uint8_t contrast = 180;
    uint8_t peakSensitivity = 96;
    uint8_t flowSpeed = 24;
    uint8_t hueModulation = 128;
};

struct SpectralIrisSpec {
    static constexpr AnimationKind kind = AnimationKind::SpectralIris;
    static constexpr Layer defaultLayer = Layer::Fft;
    static constexpr uint16_t defaultLifetimeMs = 0;
};

static_assert(std::is_trivially_copyable_v<SpectralIrisConfig>,
              "SpectralIrisConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
