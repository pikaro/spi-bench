#pragma once

#include "Button/Behavior/PressClassifier.hpp"
#include "Button/Interfaces/Config.hpp"
#include "RotaryEncoder/Interfaces/Config.hpp"
#include "RotaryEncoder/Interfaces/PositionConfig.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "Types/Gpio.hpp"
#include "Wire/I2C/Interfaces/Ina2xxConfig.hpp"
#include "Wire/I2C/Interfaces/MasterConfig.hpp"
#include <optional>

inline Totem::Wire::I2C::MasterConfig i2cMasterConfig{
    .busId = Totem::Wire::I2C::BusId::Bus0,
    .pins =
        {
            .sda = Pin::GPIO10,
            .scl = Pin::RX, // ESP32-C3 SuperMini GPIO20
        },
    .clockHz = 100'000,
    .enableInternalPullups = true,
    .transactionTimeoutMs = 50,
};

inline Totem::Wire::I2C::Ina2xxConfig ina226Config{
    .device =
        {
            .address = 0x40,
            .clockHz = 100'000,
        },
    .sampleIntervalMs = 100,
    .shuntMicroOhms = 2'000,
    .expectedMaxCurrentMicroamps = 2'000'000,
    .busVoltage =
        {
            .absoluteMinMillivolts = 0,
            .practicalMinMillivolts = 0,
            .practicalMaxMillivolts = 29'400,
            .absoluteMaxMillivolts = 30'000,
        },
    .current =
        {
            .absoluteMinMicroamps = -2'000'000,
            .practicalMinMicroamps = 0,
            .practicalMaxMicroamps = 1'800'000,
            .absoluteMaxMicroamps = 2'000'000,
        },
    .metricsGroupName = "ina226",
};

inline constexpr Totem::RotaryEncoder::Config rotaryEncoderConfig{
    .channelA =
        {
            .name = "RotaryCLK",
            .pin = Pin::GPIO1,
            .pull = GpioPull::Up,
            .debounceMs = std::nullopt,
            .pollIntervalMs = std::nullopt,
        },
    .channelB =
        {
            .name = "RotaryDT",
            .pin = Pin::GPIO0,
            .pull = GpioPull::Up,
            .debounceMs = std::nullopt,
            .pollIntervalMs = std::nullopt,
        },
};

inline constexpr Totem::Button::Config rotarySwitchConfig{
    .input =
        {
            .name = "RotarySW",
            .pin = Pin::GPIO3,
            .pull = GpioPull::Up,
            .debounceMs = 20,
        },
    .activeLow = true,
};

inline constexpr Totem::RotaryEncoder::PositionConfig menuPositionConfig{
    .initialValue = 0,
    .minimum = -8,
    .maximum = 8,
};

inline constexpr Totem::Button::Config gestureButtonConfig{
    .input =
        {
            .name = "GestureButton",
            .pin = Pin::TX, // ESP32-C3 SuperMini GPIO21
            .pull = GpioPull::Up,
            .debounceMs = 20,
        },
    .activeLow = true,
};

inline constexpr Totem::Button::Behavior::PressConfig gestureConfig{
    .longPressMs = 700,
    .doublePressMs = 300,
};

inline constexpr Totem::StatusLed::Config statusLedConfig{
    .configured = false,
};

static_assert(rotaryEncoderConfig.validate());
static_assert(rotarySwitchConfig.validate());
static_assert(menuPositionConfig.validate());
static_assert(gestureButtonConfig.validate());
