#pragma once

#include "Platform/Hardware.hpp"
#include "StaticConfig/Stacks.hpp"
#include "StaticConfig/Wifi.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "Wifi/Facade.hpp"
#include "Wire/Rs485/Interfaces/MasterConfig.hpp"
#include "Wire/Spi/Interfaces/MasterConfig.hpp"
#include "Wire/Spi/Interfaces/Types.hpp"

#if __has_include("wifi_credentials.hpp")
#include "wifi_credentials.hpp"
inline constexpr bool masterWifiCredentialsConfigured = true;
#else
namespace MasterWifiCredentials {

struct Station {
    inline static constexpr const char *ssid = "";
    inline static constexpr const char *password = "";
};

struct AccessPoint {
    inline static constexpr const char *ssid = "";
    inline static constexpr const char *password = "";
};

inline constexpr Totem::Wifi::Mode mode = Totem::Wifi::Mode::Disabled;

} // namespace MasterWifiCredentials
inline constexpr bool masterWifiCredentialsConfigured = false;
#endif

#ifndef PUBSUB_STAR_MASTER_HIGH_SPI_CLOCK_HZ
#define PUBSUB_STAR_MASTER_HIGH_SPI_CLOCK_HZ 10000000
#endif

#ifndef PUBSUB_STAR_MASTER_LOW_SPI_CLOCK_HZ
#define PUBSUB_STAR_MASTER_LOW_SPI_CLOCK_HZ 10000000
#endif

inline Totem::Wire::Rs485::MasterConfig rs485MasterConfig{
    .uartConfig =
        {
            .uartNumber = 1,
            .pins =
                {
                    .txPin = Pin::GPIO1,
                    .rxPin = Pin::GPIO2,
                },
        },
    .task =
        {
            .name = "Rs485MasterTask",
            .priority = 4,
            .core = Totem::TaskController::Config::CorePreference::specific(0),
            .stackSize = Totem::StaticConfig::TaskStacks::rs485Master,
            .intervalMs = 100,
            .noCatchup = true,
            .useNotify = true,
            .notifyExpectTimeout = false,
            .notifyTimeoutMs = 10,
        },
    .attentionPin = Pin::GPIO14,
};

inline Totem::Wire::Spi::MasterConfig spiMasterBusHighSpeedConfig{
    .bus =
        {
            .busId = Totem::Wire::Spi::BusId::Bus2,
            .pins =
                {
                    .mosiPin = Pin::GPIO15,
                    .misoPin = Pin::GPIO16,
                    .sclkPin = Pin::GPIO17,
                },
            .maxTransferSize = 4096,
        },
    .device =
        {
            .csPin = Pin::GPIO4,
            .clockHz = PUBSUB_STAR_MASTER_HIGH_SPI_CLOCK_HZ,
            .mode = Totem::Wire::Spi::Mode::Mode0,
            .bitOrder = Totem::Wire::Spi::BitOrder::MsbFirst,
            .queueSize = 1,
            .inputDelayNs = 0,
        },
    .task =
        {
            .name = "SpiGpu0Task",
            .priority = 4,
            .core = Totem::TaskController::Config::CorePreference::specific(0),
            .stackSize = Totem::StaticConfig::TaskStacks::spiMaster,
            .intervalMs = 10,
            .noCatchup = true,
            .useNotify = true,
            .notifyExpectTimeout = false,
            .notifyTimeoutMs = 10,
        },
    .serviceBudgetMs = 6,
    .maxTurnsPerStep = 4,
    .interTurnDelayMs = 1,
    .maxOutboundSlotBytes = 256,
    .attentionReceiveWindowBytes = 256,
    .localWriteCoalesceUs = 1000,
    .noSlotBackoffUs = 1000,
    .attentionPin = Pin::GPIO5,
};

inline Totem::Wire::Spi::MasterConfig spiMasterBusHighSpeedGpu1Config = [] {
    auto config = spiMasterBusHighSpeedConfig;
    config.device.csPin = Pin::GPIO11;
    config.task.name = "SpiGpu1Task";
    config.attentionPin = Pin::GPIO10;
    return config;
}();

inline Totem::Wire::Spi::MasterConfig spiMasterBusLowSpeedConfig{
    .bus =
        {
            .busId = Totem::Wire::Spi::BusId::Bus3,
            .pins =
                {
                    .mosiPin = Pin::GPIO21,
                    // PSRAM disabled
                    .misoPin = Pin::PsramGPIO36,
                    .sclkPin = Pin::PsramGPIO37,
                },
            .maxTransferSize = 4096,
        },
    .device =
        {
            .csPin = Pin::GPIO38,
            .clockHz = PUBSUB_STAR_MASTER_LOW_SPI_CLOCK_HZ,
            .mode = Totem::Wire::Spi::Mode::Mode0,
            .bitOrder = Totem::Wire::Spi::BitOrder::MsbFirst,
            .queueSize = 1,
            .inputDelayNs = 0,
        },
    .task =
        {
            .name = "SpiMediaTask",
            .priority = 4,
            .core = Totem::TaskController::Config::CorePreference::specific(1),
            .stackSize = Totem::StaticConfig::TaskStacks::spiMaster,
            .intervalMs = 10,
            .noCatchup = true,
            .useNotify = true,
            .notifyExpectTimeout = false,
            .notifyTimeoutMs = 10,
        },
    .serviceBudgetMs = 6,
    .maxTurnsPerStep = 4,
    .interTurnDelayMs = 1,
    .maxOutboundSlotBytes = 256,
    .attentionReceiveWindowBytes = 256,
    .localWriteCoalesceUs = 1000,
    .noSlotBackoffUs = 1000,
    .attentionPin = Pin::GPIO39,
};

inline constexpr Pin ledLevelShifterOutputEnablePin = Pin::GPIO13;
// The current 74AHCT124 LED output-enable line is active-low.
inline constexpr bool ledLevelShifterOutputEnabledLevel = false;

inline constexpr Totem::StatusLed::Config statusLedConfig{
    .configured = true,
    .pin = Pin::StatusLed,
};

inline constexpr Totem::Wifi::Config wifiConfig{
    .mode = MasterWifiCredentials::mode,
    .station =
        {
            .credentials =
                {
                    .ssid = MasterWifiCredentials::Station::ssid,
                    .password = MasterWifiCredentials::Station::password,
                },
            .reconnect = Totem::StaticConfig::Wifi::defaultStationReconnect,
            .maxReconnectAttempts =
                Totem::StaticConfig::Wifi::defaultStationMaxReconnectAttempts,
        },
    .accessPoint =
        {
            .credentials =
                {
                    .ssid = MasterWifiCredentials::AccessPoint::ssid,
                    .password = MasterWifiCredentials::AccessPoint::password,
                },
            .channel = Totem::StaticConfig::Wifi::defaultApChannel,
            .hidden = false,
            .maxConnections =
                Totem::StaticConfig::Wifi::defaultApMaxConnections,
        },
    .disableNvsStorage = true,
};

inline constexpr Pin ledPresentStrobeOutputPin = Pin::GPIO6;
inline constexpr uint32_t ledPresentStrobeFps = 125;
inline constexpr uint64_t ledPresentStrobeHalfPeriodUs =
    1000000ULL / (static_cast<uint64_t>(ledPresentStrobeFps) * 2ULL);
