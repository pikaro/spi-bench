#pragma once

#include <cstddef>
#include <cstdint>

namespace Totem::LedTopology {

using LogicalPixelIndex = uint16_t;
using PhysicalPixelIndex = uint16_t;
using LocalPixelIndex = uint16_t;

struct LogicalPoint {
    uint8_t spoke = 0;
    uint8_t radial = 0;
};

struct DataLineSpan {
    uint8_t line = 0;
    PhysicalPixelIndex physicalStart = 0;
    LocalPixelIndex localStart = 0;
    uint16_t count = 0;
};

} // namespace Totem::LedTopology
