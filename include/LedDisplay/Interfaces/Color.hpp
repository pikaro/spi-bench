#pragma once

#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay {

struct HsvColor {
    uint8_t hue = 0;
    uint8_t saturation = 0;
    uint8_t value = 0;
};

enum HsvConversion : uint8_t {
    Rainbow = 0,
    Spectrum,
    Fullspectrum,
};

struct RgbColor {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
};

static_assert(std::is_trivially_copyable_v<HsvColor>,
              "HsvColor must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<RgbColor>,
              "RgbColor must remain queue-copyable");

} // namespace Totem::LedDisplay
