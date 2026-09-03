#pragma once

#include "BatteryMonitor/Interfaces/Config.hpp"
#include "Platform/Hardware.hpp"
#include "StaticConfig/Stacks.hpp"
#include "StaticConfig/Wifi.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "Wifi/Facade.hpp"
#include "Wire/I2C/Interfaces/Ina2xxConfig.hpp"
#include "Wire/I2C/Interfaces/MasterConfig.hpp"
#include "Wire/I2C/Interfaces/Types.hpp"
#include "Wire/Spi/Interfaces/SlaveConfig.hpp"
#include "Wire/Spi/Interfaces/Types.hpp"
#include <cstdint>

inline constexpr Totem::BatteryMonitor::BatteryConfig batteryConfig{};
inline constexpr uint32_t batteryStatusPublishIntervalMs = 1'000;

inline constexpr Totem::Wire::I2C::MasterConfig i2cMasterConfig{
    .busId = Totem::Wire::I2C::BusId::Bus0,
    .pins =
        {
            .sda = Pin::GPIO7,
            .scl = Pin::StrappingGPIO8,
        },
    .clockHz = 100'000,
    .enableInternalPullups = false,
    .transactionTimeoutMs = 50,
};

inline constexpr Totem::Wire::I2C::Ina2xxConfig ina24vConfig{
    .device =
        {
            .address = 0x40,
            .clockHz = 100'000,
        },
    .sampleIntervalMs = 100,
    .shuntMicroOhms = 2'000,
    .expectedMaxCurrentMicroamps = 3'000'000,
    .busVoltage =
        {
            .absoluteMinMillivolts = batteryConfig.absoluteMinPackMillivolts(),
            .practicalMinMillivolts =
                batteryConfig.practicalMinPackMillivolts(),
            .practicalMaxMillivolts =
                batteryConfig.practicalMaxPackMillivolts(),
            .absoluteMaxMillivolts = batteryConfig.absoluteMaxPackMillivolts(),
        },
    .current =
        {
            .absoluteMinMicroamps = -3'000'000,
            .practicalMinMicroamps =
                -static_cast<int32_t>(batteryConfig.currentDeadbandMicroamps),
            .practicalMaxMicroamps = 2'000'000,
            .absoluteMaxMicroamps = 3'000'000,
        },
    .metricsGroupName = "ina24v",
};

inline constexpr Totem::Wire::I2C::Ina2xxConfig ina5vConfig{
    .device =
        {
            .address = 0x41,
            .clockHz = 100'000,
        },
    .sampleIntervalMs = 100,
    .shuntMicroOhms = 2'000,
    .expectedMaxCurrentMicroamps = 1'000'000,
    .current =
        {
            .absoluteMinMicroamps = -1'000'000,
            .practicalMinMicroamps =
                -static_cast<int32_t>(batteryConfig.currentDeadbandMicroamps),
            .practicalMaxMicroamps = 500'000,
            .absoluteMaxMicroamps = 1'000'000,
        },
    .metricsGroupName = "ina5v",
};

inline Totem::Wire::Spi::SlaveConfig spiSlaveConfig{
    .busId = Totem::Wire::Spi::BusId::Bus2,
    .pins =
        {
            .mosiPin = Pin::GPIO1,
            .misoPin = Pin::GPIO4,
            .sclkPin = Pin::GPIO3,
        },
    .csPin = Pin::GPIO0,
    .maxTransferSize = 4096,
    .transferWindowBytes = 256,
    .maxOutboundSlotBytes = 256,
    .mode = Totem::Wire::Spi::Mode::Mode0,
    .queueSize = 1,
    .task =
        {
            .name = "SpiSlaveTask",
            .priority = 4,
            .stackSize = Totem::StaticConfig::TaskStacks::spiSlave,
            .intervalMs = 10,
            .noCatchup = true,
            .useNotify = true,
            .notifyExpectTimeout = false,
            .notifyTimeoutMs = 10,
        },
    .attentionPin = Pin::GPIO10,
};

inline constexpr Totem::StatusLed::Config statusLedConfig{
    .configured = false,
};

inline constexpr Totem::Wifi::Config wifiConfig{
    .mode = Totem::Wifi::Mode::AccessPoint,
    .station =
        Totem::Wifi::StationConfig{
            .credentials =
                {
                    .ssid = "dre-guest",
                    .passwordSecretName = "wifi-sta-pass",
                },
            .reconnect = Totem::StaticConfig::Wifi::defaultStationReconnect,
            .maxReconnectAttempts =
                Totem::StaticConfig::Wifi::defaultStationMaxReconnectAttempts,
        },
    .accessPoint =
        Totem::Wifi::AccessPointConfig{
            .credentials =
                {
                    .ssid = "totem",
                    .passwordSecretName = "wifi-ap-pass",
                },
            .channel = Totem::StaticConfig::Wifi::defaultApChannel,
            .hidden = false,
            .maxConnections =
                Totem::StaticConfig::Wifi::defaultApMaxConnections,
        },
    .disableNvsStorage = true,
};

static_assert(batteryConfig.validate());
static_assert(i2cMasterConfig.validate());
static_assert(ina24vConfig.validate());
static_assert(ina5vConfig.validate());
static_assert(wifiConfig.validate());
static_assert(wifiConfig.station.has_value());
static_assert(wifiConfig.station->validate());
static_assert(wifiConfig.accessPoint.has_value());
static_assert(wifiConfig.accessPoint->validate());
