#pragma once

#include "Platform/Hardware.hpp"
#include "StatusLed/Interfaces/Types.hpp"
#include <cstdint>

namespace Totem::StatusLed {

enum class ColorOrder : uint8_t {
    RGB,
    GRB,
};

enum class OutputBackend : uint8_t {
    RmtWs2812,
    SplitRgbGpio,
};

struct SplitRgbGpioConfig {
    Pin red{};
    Pin green{};
    Pin blue{};
    bool activeHigh = true;

    [[nodiscard]] constexpr bool validate() const {
        return red != green && red != blue && green != blue;
    }
};

struct Config {
    bool configured = false;
    Pin pin{};
    ColorOrder colorOrder = ColorOrder::GRB;
    OutputBackend backend = OutputBackend::RmtWs2812;
    SplitRgbGpioConfig splitRgbGpio{};
    BrightnessMultiplier brightness = BrightnessMultiplier::fromPercent(30);

    [[nodiscard]] constexpr bool validate() const {
        if (!configured) {
            return true;
        }

        if (!brightness.validate()) {
            return false;
        }

        switch (backend) {
        case OutputBackend::RmtWs2812:
            return true;
        case OutputBackend::SplitRgbGpio:
            return splitRgbGpio.validate();
        default:
            return false;
        }
    }
};

} // namespace Totem::StatusLed
