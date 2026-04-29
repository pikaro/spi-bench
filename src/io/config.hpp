#pragma once

#include "Platform/Hardware.hpp"
#include "Wire/Rs485/Interfaces/SlaveConfig.hpp"
#include "Wire/Spi/Interfaces/SlaveConfig.hpp"
#include "Wire/Spi/Interfaces/Types.hpp"

inline Totem::Wire::Rs485::SlaveConfig rs485SlaveConfig{
    .uartConfig =
        {
            .uartNumber = 1,
            .pins =
                {
                    .txPin = Pin::GPIO6,
                    .rxPin = Pin::GPIO5,
                },
        },
    .attentionPin = Pin::GPIO10,
};

inline Totem::Wire::Spi::SlaveConfig spiSlaveConfig{
    .busId = Totem::Wire::Spi::BusId::Bus2,
    .pins =
        {
            .mosiPin = Pin::GPIO3,
            .misoPin = Pin::GPIO7,
            .sclkPin = Pin::GPIO4,
        },
    .csPin = Pin::GPIO1,
    .maxTransferSize = 4096,
    .mode = Totem::Wire::Spi::Mode::Mode0,
    .bitOrder = Totem::Wire::Spi::BitOrder::MsbFirst,
    .queueSize = 1,
};
