#pragma once

#include "Platform/Hardware.hpp"
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

    [[nodiscard]] constexpr bool validate() const {
        if (!configured) {
            return true;
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
