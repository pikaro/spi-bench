#pragma once

#include "Data/Peripherals.hpp"
#include "LedPwm/detail/PlatformSelect.hpp"
#include "Platform/PlatformSelect.hpp"
#include "StaticConfig/LedPwm.hpp"
#include "StaticConfig/Stacks.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include <array>
#include <cmath>

namespace Totem::LedPwm {

struct PeripheralLedConfig {
    PeripheralLed led;
    Pin pin;
    bool configured = false;

    [[nodiscard]] static bool validate() { return true; }
};

struct Config {
    Totem::TaskController::Config task{
        .name = "LedPwm",
        .stackSize = StaticConfig::TaskStacks::ledPwm,
        .intervalMs = 10,
    };

    detail::PlatformConfig platform{};

    std::array<PeripheralLedConfig, LedPwmConfig::maxLeds> leds;

    [[nodiscard]] bool validate() const {
        return task.validate() && platform.validate();
    }
};

} // namespace Totem::LedPwm
