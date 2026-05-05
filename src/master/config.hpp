#pragma once

#include "Platform/Hardware.hpp"
#include "Wire/Rs485/Interfaces/MasterConfig.hpp"
#include "Wire/Spi/Interfaces/MasterConfig.hpp"
#include "Wire/Spi/Interfaces/Types.hpp"

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
            .priority = 2,
            .core = Totem::TaskController::Config::CorePreference::specific(0),
            .stackSize = 8192,
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
            .priority = 2,
            .core = Totem::TaskController::Config::CorePreference::specific(0),
            .stackSize = 8192,
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
            .priority = 2,
            .core = Totem::TaskController::Config::CorePreference::specific(1),
            .stackSize = 8192,
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

inline constexpr Pin levelShifterEnablePin = Pin::GPIO13;
