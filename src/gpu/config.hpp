#pragma once

#include "Platform/Hardware.hpp"
#include "Wire/Spi/Interfaces/SlaveConfig.hpp"
#include "Wire/Spi/Interfaces/Types.hpp"

#if (NODE_IDENTITY == GPUNode0)

#define GPU_MOSI GPIO4
#define GPU_MISO GPIO5
#define GPU_SCLK GPIO6
#define GPU_CS GPIO7
#define GPU_ATTENTION GPIO8

#elif (NODE_IDENTITY == GPUNode1)

#define GPU_MOSI GPIO9
#define GPU_MISO GPIO8
#define GPU_SCLK GPIO7
#define GPU_CS GPIO6
#define GPU_ATTENTION GPIO5

#endif

inline Totem::Wire::Spi::SlaveConfig spiSlaveConfig{
    .busId = Totem::Wire::Spi::BusId::Bus2,
    .pins =
        {
            .mosiPin = Pin::GPU_MOSI,
            .misoPin = Pin::GPU_MISO,
            .sclkPin = Pin::GPU_SCLK,
        },
    .csPin = Pin::GPU_CS,
    .maxTransferSize = 4096,
    .queueSize = 1,
    .attentionPin = Pin::GPU_ATTENTION,
};
