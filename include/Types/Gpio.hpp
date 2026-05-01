#pragma once

#include "Platform/Hardware.hpp"
#include "Types/Error.hpp"
#include <cstdint>

enum class GpioPull : uint8_t {
    None,
    Up,
    Down,
};

enum class GpioOutputMode : uint8_t {
    PushPull,
    OpenDrain,
};

enum class GpioInterrupt : uint8_t {
    Disabled,
    Rising,
    Falling,
    AnyEdge,
};

enum class GpioEventType : uint8_t {
    Rising,
    Falling,
    Changed,
};

struct GpioEvent {
    Pin pin;
    GpioEventType type = GpioEventType::Changed;
    bool level = false;
    int64_t timestampUs = 0;
};

using GpioEventCallback = ReturnCode (*)(void *owner, GpioEvent event);
using GpioIsrCallback = void (*)(void *owner, GpioEvent event);
