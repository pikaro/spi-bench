#pragma once

#include "Data/Peripherals.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>

namespace Totem::Buttons {

enum class ButtonEventType : uint8_t {
    Pressed,
    Released,
};

struct WIRE_MSG ButtonEvent {
    ButtonEventType type;
    PeripheralButton button;
};

} // namespace Totem::Buttons
