#pragma once

#include "Data/Peripherals.hpp"
#include "LedPwm/Interfaces/Types.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedPwm {

enum class CommandEventType : uint8_t {
    None = 0,
    SetBrightness,
    StartPulse,
    StartGlitter,
    ClearAnimations,
};

struct WIRE_MSG CommandEvent {
    PeripheralLed led = PeripheralLed::Bulb1;
    CommandEventType type = CommandEventType::None;
    Brightness brightness{};
    Pulse pulse{};
    Glitter glitter{};

    static constexpr CommandEvent setBrightness(PeripheralLed led,
                                                Brightness brightness) {
        return CommandEvent{
            .led = led,
            .type = CommandEventType::SetBrightness,
            .brightness = brightness,
        };
    }

    static constexpr CommandEvent startPulse(PeripheralLed led, Pulse pulse) {
        return CommandEvent{
            .led = led,
            .type = CommandEventType::StartPulse,
            .pulse = pulse,
        };
    }

    static constexpr CommandEvent startGlitter(PeripheralLed led,
                                               Glitter glitter) {
        return CommandEvent{
            .led = led,
            .type = CommandEventType::StartGlitter,
            .glitter = glitter,
        };
    }

    static constexpr CommandEvent clearAnimations(PeripheralLed led) {
        return CommandEvent{
            .led = led,
            .type = CommandEventType::ClearAnimations,
        };
    }

    [[nodiscard]] constexpr bool validate() const {
        switch (type) {
        case CommandEventType::SetBrightness:
            return true;
        case CommandEventType::StartPulse:
            return pulse.validate();
        case CommandEventType::StartGlitter:
            return glitter.validate();
        case CommandEventType::ClearAnimations:
            return true;
        case CommandEventType::None:
        default:
            return false;
        }
    }
};

static_assert(std::is_trivially_copyable_v<CommandEvent>,
              "CommandEvent must remain queue-copyable");

} // namespace Totem::LedPwm
