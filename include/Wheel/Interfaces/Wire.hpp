#pragma once

#include "Macros/internal/Markers.hpp"
#include "Types/Angle.hpp"
#include <cstdint>

namespace Totem::Wheel {

struct WIRE_MSG WheelState {
    Angle<uint16_t> position;
    Angle<uint16_t> delta;
};

} // namespace Totem::Wheel
