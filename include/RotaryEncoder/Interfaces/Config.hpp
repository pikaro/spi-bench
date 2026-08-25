#pragma once

#include "DigitalInput/Interfaces/Config.hpp"
#include "RotaryEncoder/Interfaces/PositionConfig.hpp"
#include <cstdint>
#include <optional>

namespace Totem::RotaryEncoder {

struct Config {
    // Quadrature decoding relies on the raw interrupt order. Debounce and
    // polling must remain disabled; the transition table rejects contact
    // bounce without delaying or independently reordering the two channels.
    DigitalInput::Config channelA{
        .name = "EncoderA",
        .debounceMs = std::nullopt,
        .pollIntervalMs = std::nullopt,
    };
    DigitalInput::Config channelB{
        .name = "EncoderB",
        .debounceMs = std::nullopt,
        .pollIntervalMs = std::nullopt,
    };
    uint8_t transitionsPerDetent = 4;
    bool reverseDirection = false;
    PositionConfig position{};

    [[nodiscard]] constexpr bool validate() const {
        return channelA.validate() && channelB.validate() &&
               channelA.pin != channelB.pin &&
               !channelA.debounceMs.has_value() &&
               !channelB.debounceMs.has_value() &&
               !channelA.pollIntervalMs.has_value() &&
               !channelB.pollIntervalMs.has_value() &&
               (transitionsPerDetent == 1 || transitionsPerDetent == 2 ||
                transitionsPerDetent == 4) &&
               position.validate();
    }
};

} // namespace Totem::RotaryEncoder
