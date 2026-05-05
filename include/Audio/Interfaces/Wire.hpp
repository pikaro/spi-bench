#pragma once

#include "Audio/Interfaces/Types.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>

namespace Totem::Audio {

struct WIRE_MSG FftFrame {
    uint16_t subBass;
    uint16_t bass;
    uint16_t lowMid;
    uint16_t mid;
    uint16_t highMid;
    uint16_t presence;
    uint16_t brilliance;
    uint16_t air;
};

struct WIRE_MSG BeatEvent {
    BeatGroup group;
    uint8_t bpm;
    uint8_t energy;
    uint8_t tension;
};

} // namespace Totem::Audio
