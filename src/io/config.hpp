#pragma once

#include "Bluetooth/Interfaces/Config.hpp"
#include "Button/Interfaces/Config.hpp"
#include "Data/Facade.hpp"
#include "LedPwm/Interfaces/Config.hpp"
#include "Platform/Hardware.hpp"
#include "StaticConfig/Stacks.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "Types/Gpio.hpp"
#include "Wheel/Interfaces/Config.hpp"
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
    .platform =
        {
            .brightnessGamma = 1.0F,
        },
    .leds = {{
        {.led = PeripheralLed::Bulb1, .pin = Pin::GPIO6, .configured = true},
        {.led = PeripheralLed::Bulb2, .pin = Pin::GPIO7, .configured = true},
        {.led = PeripheralLed::Onboard, .pin = Pin::GPIO10, .configured = true},
    }},
};

constexpr Totem::Button::Config bellButtonConfig{
    .input =
        {
            .name = "Bell",
            .pin = Pin::GPIO4,
            .pull = GpioPull::None,
        },
    .activeLow = false,
};

constexpr Totem::Button::Config calibrationButtonConfig{
    .input =
        {
            .name = "Calibration",
            .pin = Pin::StrappingGPIO9,
            .pull = GpioPull::Down,
        },
    .activeLow = false,
    .notifyReleased = false,
};

static_assert(calibrationButtonConfig.input.pin !=
                  rs485SlaveConfig.uartConfig.pins.rxPin,
              "Calibration button must not share the IO RS485 RX pin");
static_assert(calibrationButtonConfig.input.pin !=
                  rs485SlaveConfig.uartConfig.pins.txPin,
              "Calibration button must not share the IO RS485 TX pin");
static_assert(calibrationButtonConfig.input.pin !=
                  rs485SlaveConfig.attentionPin,
              "Calibration button must not share the IO RS485 attention pin");

inline constexpr Totem::StatusLed::Config statusLedConfig{
    .configured = false,
};

inline constexpr Totem::Wheel::BleWheelConfig wheelConfig{};

inline Totem::Bluetooth::Config
makeBluetoothConfig(Totem::Bluetooth::IDeviceDriver &wheelDriver) {
    auto config = Totem::Bluetooth::Config{};
    config.drivers[0] = &wheelDriver;
    config.driverCount = 1;
    config.scanInterval = 160;
    config.scanWindow = 160;
    return config;
}
