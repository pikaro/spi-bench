#pragma once

#include "Data/Peripherals.hpp"
#include "Platform/Hardware.hpp"
#include "StaticConfig/Button.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "Types/Gpio.hpp"
#include <array>

namespace Totem::Buttons {

struct PeripheralButtonConfig {
    Pin pin;
    PeripheralButton button;
    GpioPull pull = GpioPull::Up;

    bool activeLow = true;
    bool notifyPressed = true;
    bool notifyReleased = true;

    [[nodiscard]] constexpr bool validate() const {
        return notifyPressed || notifyReleased;
    }
};

struct Config {
    Totem::TaskController::Config task{
        .name = "Buttons",
        .stackSize = 4096,
        .intervalMs = 100,
        .useNotify = true,
    };

    std::array<PeripheralButtonConfig, ButtonConfig::maxButtons> buttons;

    [[nodiscard]] bool validate() const {
        if (!task.validate()) {
            return false;
        }
        for (const auto &button : buttons) {
            if (!button.validate()) {
                return false;
            }
        }
        return true;
    }
};

} // namespace Totem::Buttons
