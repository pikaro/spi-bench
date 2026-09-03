#pragma once

#include <cmath>
#include <cstdint>

namespace Totem::LedPwm::detail::platform {

struct Config {
    uint32_t frequency = 4000;
    uint32_t initialDuty = 0;
    uint8_t resolution = 14;
    float brightnessGamma = 2.2F;
    bool outputInverted = false;

    [[nodiscard]] bool validate() const {
        return std::isfinite(brightnessGamma) && brightnessGamma > 0.0F;
    }
};

} // namespace Totem::LedPwm::detail::platform
