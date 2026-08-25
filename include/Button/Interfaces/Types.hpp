#pragma once

#include <cstdint>

namespace Totem::Button {

enum class Event : uint8_t {
    Pressed,
    Released,
    Press,
    LongPress,
    DoublePress,
};

} // namespace Totem::Button
