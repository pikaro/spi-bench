#pragma once

#include "Audio/Interfaces/Types.hpp"
#include "Wire/Spi/Interfaces/SlaveConfig.hpp"

inline Totem::Audio::I2SSourceConfig i2sSourceConfig{
    .device = Totem::Audio::I2SDevicePreset::LegacySoundCard,
};

inline Totem::Audio::FftAnalyzerConfig fftAnalyzerConfig{};

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
    .transferWindowBytes = 256,
    .maxOutboundSlotBytes = 256,
    .mode = Totem::Wire::Spi::Mode::Mode0,
    .queueSize = 1,
    .task =
        {
            .name = "SpiSlaveTask",
            .priority = 2,
            .stackSize = 8192,
            .intervalMs = 10,
            .noCatchup = true,
            .useNotify = true,
            .notifyExpectTimeout = false,
            .notifyTimeoutMs = 10,
        },
    .attentionPin = Pin::GPIO17,
};
