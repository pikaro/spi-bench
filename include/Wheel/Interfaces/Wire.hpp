#pragma once

#include "Macros/internal/Markers.hpp"
#include <cstdint>

namespace Totem::Wheel {

struct WIRE_MSG WheelState {
    uint16_t position;
    uint8_t delta;
};

} // namespace Totem::Wheel
