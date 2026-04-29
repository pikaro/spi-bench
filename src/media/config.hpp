#pragma once

#include "Wire/Spi/Interfaces/SlaveConfig.hpp"

inline Totem::Wire::Spi::SlaveConfig spiSlaveConfig{
    .busId = Totem::Wire::Spi::BusId::Bus3,
    .pins =
        {
            .mosiPin = Pin::VSPI_MOSI,
            .misoPin = Pin::VSPI_MISO,
            .sclkPin = Pin::VSPI_SCLK,
        },
    .csPin = Pin::VSPI_CS,
    .maxTransferSize = 4096,
    .mode = Totem::Wire::Spi::Mode::Mode0,
    .queueSize = 1,
    .attentionPin = Pin::GPIO17,
};
