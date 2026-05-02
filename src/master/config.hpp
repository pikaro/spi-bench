#pragma once

#include "Platform/Hardware.hpp"
#include "Wire/Rs485/Interfaces/MasterConfig.hpp"
#include "Wire/Spi/Interfaces/MasterConfig.hpp"
#include "Wire/Spi/Interfaces/Types.hpp"

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
            .clockHz = 10'000'000,
            .mode = Totem::Wire::Spi::Mode::Mode0,
            .bitOrder = Totem::Wire::Spi::BitOrder::MsbFirst,
            .queueSize = 1,
            .inputDelayNs = 0,
        },
    .attentionPin = Pin::GPIO5,
    // {
    //     .csPin = Pin::GPIO11,
    //     .clockHz = 20'000'000,
    //     .queueSize = 1,
    //     // attention GPIO10
    // },
};

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
            .clockHz = 10'000'000,
            .mode = Totem::Wire::Spi::Mode::Mode0,
            .bitOrder = Totem::Wire::Spi::BitOrder::MsbFirst,
            .queueSize = 1,
            .inputDelayNs = 0,
        },
    .task =
        {
            .name = "SpiMasterTask",
            .priority = 2,
            .stackSize = 8192,
            .intervalMs = 10,
            .noCatchup = true,
            .useNotify = true,
            .notifyExpectTimeout = false,
            .notifyTimeoutMs = 10,
        },
    .serviceBudgetMs = 5,
    .maxTurnsPerStep = 1,
    .interTurnDelayMs = 0,
    .maxOutboundSlotBytes = 256,
    .attentionReceiveWindowBytes = 256,
    .localWriteCoalesceUs = 1000,
    .noSlotBackoffUs = 1000,
    .attentionPin = Pin::GPIO39,
};
