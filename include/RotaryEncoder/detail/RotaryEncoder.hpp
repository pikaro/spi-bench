#pragma once

#include "Generic/StateMachine.hpp"
#include "Macros/Facade.hpp"
#include <array>
#include <cstdint>

namespace Totem::RotaryEncoder::detail {

enum class RotaryEncoderState : uint8_t {
    Invalid = 0,
    ALowBLow,
    ALowBHigh,
    AHighBLow,
    AHighBHigh,
};

enum class RotaryEncoderEvent : uint8_t {
    Default = 0,
    AFalling,
    ARising,
    BFalling,
    BRising,
};

constexpr std::array rotaryEncoderTransitions{
    TRANSITION(RotaryEncoder, ALowBLow, AHighBLow, ARising),
    TRANSITION(RotaryEncoder, ALowBHigh, ALowBLow, BFalling),
    TRANSITION(RotaryEncoder, AHighBLow, ALowBLow, AFalling),
    TRANSITION(RotaryEncoder, AHighBHigh, AHighBLow, BFalling),

    TRANSITION(RotaryEncoder, ALowBLow, ALowBHigh, BRising),
    TRANSITION(RotaryEncoder, ALowBHigh, AHighBHigh, ARising),
    TRANSITION(RotaryEncoder, AHighBLow, AHighBHigh, BRising),
    TRANSITION(RotaryEncoder, AHighBHigh, ALowBHigh, AFalling),
};

} // namespace Totem::RotaryEncoder::detail
