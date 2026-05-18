#pragma once

#include "Data.hpp"
#include "Platform/Hardware.hpp"
#include "StaticConfig/Stacks.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "Types/Gpio.hpp"
#include "Wire/Spi/Interfaces/SlaveConfig.hpp"
#include "Wire/Spi/Interfaces/Types.hpp"

namespace {

inline constexpr auto gpuNodeName = Totem::Data::NodeName::NODE_IDENTITY;

[[nodiscard]] constexpr Totem::Wire::Spi::BusPins gpuSpiPins() {
    if constexpr (gpuNodeName == Totem::Data::NodeName::GPUNode1) {
        return {
            .mosiPin = Pin::GPIO9,
            .misoPin = Pin::GPIO8,
            .sclkPin = Pin::GPIO7,
        };
    }
    return {
        .mosiPin = Pin::GPIO4,
        .misoPin = Pin::GPIO5,
        .sclkPin = Pin::GPIO6,
    };
}

[[nodiscard]] constexpr Pin gpuCsPin() {
    if constexpr (gpuNodeName == Totem::Data::NodeName::GPUNode1) {
        return Pin::GPIO6;
    }
    return Pin::GPIO7;
}

[[nodiscard]] constexpr Pin gpuAttentionPin() {
    if constexpr (gpuNodeName == Totem::Data::NodeName::GPUNode1) {
        return Pin::GPIO5;
    }
    return Pin::GPIO8;
}

} // namespace

inline Totem::Wire::Spi::SlaveConfig spiSlaveConfig{
    .busId = Totem::Wire::Spi::BusId::Bus2,
    .pins = gpuSpiPins(),
    .csPin = gpuCsPin(),
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
    .attentionPin = gpuAttentionPin(),
};

inline constexpr Totem::StatusLed::Config statusLedConfig{
    .configured = true,
    .pin = Pin::StatusLed,
};

inline constexpr Pin ledPresentStrobeInputPin = Pin::GPIO10;
inline constexpr GpioPull ledPresentStrobeInputPull = GpioPull::Down;
