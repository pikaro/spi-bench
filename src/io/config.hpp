#pragma once

#include "Buttons/Interfaces/Config.hpp"
#include "Data/Facade.hpp"
#include "LedPwm/Interfaces/Config.hpp"
#include "Platform/Hardware.hpp"
#include "StaticConfig/Stacks.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "Types/Gpio.hpp"
#include "Wire/Rs485/Interfaces/SlaveConfig.hpp"

constexpr Totem::Wire::Rs485::SlaveConfig rs485SlaveConfig{
    .uartConfig =
        {
            .uartNumber = 1,
            .pins =
                {
                    .txPin = Pin::GPIO1,
                    .rxPin = Pin::GPIO0,
                },
        },
    .task =
        {
            .name = "Rs485SlaveTask",
            .priority = 4,
            .stackSize = Totem::StaticConfig::TaskStacks::rs485Slave,
            .intervalMs = 100,
            .noCatchup = true,
            .useNotify = true,
            .notifyExpectTimeout = false,
            .notifyTimeoutMs = 10,
        },
    .attentionPin = Pin::GPIO5,
};

constexpr Totem::LedPwm::Config ledPwmConfig{
    .leds = {{
        {.led = PeripheralLed::Bulb1, .pin = Pin::GPIO6, .configured = true},
        {.led = PeripheralLed::Bulb2, .pin = Pin::GPIO7, .configured = true},
        {.led = PeripheralLed::Onboard, .pin = Pin::GPIO10, .configured = true},
    }},
};

constexpr Totem::Buttons::Config buttonsConfig{
    .buttons = {{
        {
            .pin = Pin::GPIO4,
            .button = PeripheralButton::Bell,
            .pull = GpioPull::None,
            .activeLow = false,
        },
    }},
};

inline constexpr Totem::StatusLed::Config statusLedConfig{
    .configured = false,
};
