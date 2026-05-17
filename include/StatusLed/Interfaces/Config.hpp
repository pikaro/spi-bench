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
};

struct Config {
    bool configured = false;
    Pin pin{};
    ColorOrder colorOrder = ColorOrder::GRB;
    OutputBackend backend = OutputBackend::RmtWs2812;

    [[nodiscard]] constexpr bool validate() const { return true; }
};

} // namespace Totem::StatusLed
