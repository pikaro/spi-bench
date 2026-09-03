#pragma once

#include "Button/Interfaces/Types.hpp"
#include "Data/Peripherals.hpp"
#include "Macros/internal/Markers.hpp"

namespace Totem::Data {

/** Application-level button identity attached by the publishing caller. */
struct WIRE_MSG ButtonEvent {
    Totem::Button::Event event;
    PeripheralButton button;
};

} // namespace Totem::Data
