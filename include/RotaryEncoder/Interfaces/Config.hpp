#pragma once

#include "Platform/Hardware.hpp"
#include <cstdint>
#include <optional>
namespace Totem::RotaryEncoder {

struct Config {
    Pin pinA;
    Pin pinB;
    std::optional<Pin> pinButton = std::nullopt;

    bool enableChannelPullups = false;
    bool enableButtonPullup = false;

    bool enableChannelInterrupts = true;
    bool enableButtonInterrupt = true;

    uint8_t debounceTimeMs = 5;
    bool isDualMode = true;
};

} // namespace Totem::RotaryEncoder
