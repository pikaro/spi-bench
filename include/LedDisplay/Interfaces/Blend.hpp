#pragma once

#include <cstdint>

namespace Totem::LedDisplay {

enum class BlendOp : uint8_t {
    Replace = 0,
    MaxValue,
    AddValue,
    Alpha,
};

} // namespace Totem::LedDisplay
