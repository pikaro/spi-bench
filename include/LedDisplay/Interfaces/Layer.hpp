#pragma once

#include <cstdint>

namespace Totem::LedDisplay {

enum class Layer : uint8_t {
    Background = 0,
    Fft,
    Effect,
    Wheel,
    Debug,
};

} // namespace Totem::LedDisplay
