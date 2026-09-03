#pragma once

#include "Bluetooth/Interfaces/Config.hpp"
#include "Button/Interfaces/Config.hpp"
#include "Data/Facade.hpp"
#include "LedPwm/Interfaces/Config.hpp"
#include "Platform/Hardware.hpp"
#include "RotaryEncoder/Behavior/Dial.hpp"
#include "RotaryEncoder/Interfaces/Config.hpp"
#include "StaticConfig/Stacks.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "Types/Gpio.hpp"
#include "Wheel/Interfaces/Config.hpp"
#include "Wire/Rs485/Interfaces/SlaveConfig.hpp"
#include <optional>

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

inline constexpr Totem::RotaryEncoder::Config rotaryEncoderConfig{
    .channelA =
        {
            .name = "RotaryCLK",
            .pin = Pin::StrappingGPIO2,
            .pull = GpioPull::Up,
            .debounceMs = std::nullopt,
            .pollIntervalMs = std::nullopt,
        },
    .channelB =
        {
            .name = "RotaryDT",
            .pin = Pin::GPIO3,
            .pull = GpioPull::Up,
            .debounceMs = std::nullopt,
            .pollIntervalMs = std::nullopt,
        },
};

inline constexpr Totem::Button::Config rotarySwitchConfig{
    .input =
        {
            .name = "RotarySW",
            .pin = Pin::StrappingGPIO8,
            .pull = GpioPull::Up,
            .debounceMs = 20,
        },
    .activeLow = true,
};

inline constexpr Totem::RotaryEncoder::Behavior::DialConfig
    brightnessDialConfig{
        .position =
            {
                .initialValue = 16,
                .minimum = 0,
                .maximum = 31,
            },
    };

inline constexpr Totem::RotaryEncoder::PositionConfig mainMenuPositionConfig{
    .initialValue = Totem::Data::mainMenuInitialPosition,
    .minimum = Totem::Data::mainMenuMinimumPosition,
    .maximum = Totem::Data::mainMenuMaximumPosition,
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
static_assert(rotaryEncoderConfig.validate());
static_assert(rotarySwitchConfig.validate());
static_assert(brightnessDialConfig.validate());
static_assert(mainMenuPositionConfig.validate());
static_assert(rotarySwitchConfig.input.pin != rotaryEncoderConfig.channelA.pin);
static_assert(rotarySwitchConfig.input.pin != rotaryEncoderConfig.channelB.pin);

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
