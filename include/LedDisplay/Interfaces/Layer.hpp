#pragma once

#include <cstdint>

namespace Totem::LedDisplay {

enum class Layer : uint8_t {
    Background = 0,
    Fft,
    FftAlt,
    Effect,
    TransientEffect,
    Wheel,
    Debug,
};

} // namespace Totem::LedDisplay
