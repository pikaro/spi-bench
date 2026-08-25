#pragma once

#include "Platform/Hardware.hpp"
#include "Types/Gpio.hpp"
#include <cstdint>

namespace Totem::DigitalInput {

enum class EventSource : uint8_t {
    Interrupt,
    Poll,
};

struct Event {
    // Direct events retain the raw observation time. Debounced events use the
    // time at which work() accepted the stable level.
    int64_t timestampUs = 0;
    Pin pin{};
    GpioEventType type = GpioEventType::Changed;
    EventSource source = EventSource::Interrupt;
    bool level = false;
};

} // namespace Totem::DigitalInput
