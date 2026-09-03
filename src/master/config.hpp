#pragma once

#include "Platform/Hardware.hpp"
#include "StaticConfig/Stacks.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "Wire/Rs485/Interfaces/MasterConfig.hpp"
#include "Wire/Spi/Interfaces/MasterConfig.hpp"
#include "Wire/Spi/Interfaces/Types.hpp"
#include <array>

#ifndef PUBSUB_STAR_MASTER_HIGH_SPI_CLOCK_HZ
#define PUBSUB_STAR_MASTER_HIGH_SPI_CLOCK_HZ 40'000'000
#endif

#ifndef PUBSUB_STAR_MASTER_LOW_SPI_CLOCK_HZ
#define PUBSUB_STAR_MASTER_LOW_SPI_CLOCK_HZ 40'000'000
#endif

inline Totem::Wire::Rs485::MasterConfig rs485MasterConfig{
    .uartConfig =
        {
            .uartNumber = 1,
            .pins =
                {
                    .txPin = Pin::PadGPIO15,
                    .rxPin = Pin::PadGPIO14,
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
    .attentionPin = Pin::PadGPIO16,
};

inline Totem::Wire::Spi::MasterConfig spiMasterBusHighSpeedConfig{
    .bus =
        {
            .busId = Totem::Wire::Spi::BusId::Bus2,
            .pins =
                {
                    .mosiPin = Pin::GPIO11,
                    .misoPin = Pin::GPIO13,
                    .sclkPin = Pin::GPIO12,
                },
            .maxTransferSize = 4096,
        },
    .device =
        {
            .csPin = Pin::GPIO8,
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
    .attentionPin = Pin::GPIO7,
};

inline Totem::Wire::Spi::MasterConfig spiMasterBusHighSpeedGpu1Config = [] {
    auto config = spiMasterBusHighSpeedConfig;
    config.device.csPin = Pin::RX;
    config.task.name = "SpiGpu1Task";
    config.attentionPin = Pin::TX;
    return config;
}();

inline Totem::Wire::Spi::MasterConfig spiMasterBusLowSpeedConfig{
    .bus =
        {
            .busId = Totem::Wire::Spi::BusId::Bus3,
            .pins =
                {
                    .mosiPin = Pin::StrappingGPIO3,
                    .misoPin = Pin::GPIO1,
                    .sclkPin = Pin::GPIO2,
                },
            .maxTransferSize = 4096,
        },
    .device =
        {
            .csPin = Pin::GPIO4,
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
    .attentionPin = Pin::GPIO5,
};

inline Totem::Wire::Spi::MasterConfig spiMasterBusLowSpeedPowerConfig = [] {
    auto config = spiMasterBusLowSpeedConfig;
    config.device.csPin = Pin::GPIO6;
    config.task.name = "SpiPowerTask";
    config.attentionPin = Pin::GPIO9;
    return config;
}();

inline constexpr std::array<Pin, 4> spiChipSelectPins{
    Pin::GPIO4,
    Pin::GPIO6,
    Pin::GPIO8,
    Pin::RX,
};

inline constexpr Totem::StatusLed::Config statusLedConfig{
    .configured = true,
    .pin = Pin::StatusLed,
};

inline constexpr Pin ledPresentStrobeOutputPin = Pin::GPIO10;
inline constexpr uint32_t ledPresentStrobeFps = 100;
inline constexpr uint64_t ledPresentStrobeHalfPeriodUs =
    1000000ULL / (static_cast<uint64_t>(ledPresentStrobeFps) * 2ULL);
