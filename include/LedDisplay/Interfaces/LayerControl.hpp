#pragma once

#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay {

struct WIRE_MSG LayerActive {
    Layer layer = Layer::Effect;
    bool active = true;
};

struct WIRE_MSG LayerOpacity {
    Layer layer = Layer::Effect;
    uint8_t opacity = 255;
};

struct WIRE_MSG LayerFadeSwap {
    Layer first = Layer::Fft;
    Layer second = Layer::FftAlt;
    uint16_t durationMs = 10000;
};

static_assert(std::is_trivially_copyable_v<LayerActive>,
              "LayerActive must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<LayerOpacity>,
              "LayerOpacity must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<LayerFadeSwap>,
              "LayerFadeSwap must remain queue-copyable");

} // namespace Totem::LedDisplay
