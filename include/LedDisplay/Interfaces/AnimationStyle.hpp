#pragma once

#include "LedDisplay/Interfaces/Blend.hpp"
#include <cstdint>

namespace Totem::LedDisplay {

struct AnimationStyle {
    BlendOp blendOp = BlendOp::MaxValue;
    uint8_t opacity = 255;
};

} // namespace Totem::LedDisplay
