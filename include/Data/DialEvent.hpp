#pragma once

#include "Data/Peripherals.hpp"
#include "Macros/internal/Markers.hpp"
#include "RotaryEncoder/Interfaces/Types.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::Data {

struct WIRE_MSG DialEvent {
    int32_t position;
    Totem::RotaryEncoder::Direction event;
    PeripheralDial dial;
    uint8_t value;
};

static_assert(sizeof(DialEvent) == 8);
static_assert(std::is_trivially_copyable_v<DialEvent>);

} // namespace Totem::Data
