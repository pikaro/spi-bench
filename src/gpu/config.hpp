#pragma once

#include "Data.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
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
            .mosiPin = Pin::StrappingGPIO3,
            .misoPin = Pin::GPIO1,
            .sclkPin = Pin::GPIO2,
        };
    }
    return {
        .mosiPin = Pin::GPIO11,
        .misoPin = Pin::GPIO13,
        .sclkPin = Pin::GPIO12,
    };
}

[[nodiscard]] constexpr Pin gpuCsPin() {
    if constexpr (gpuNodeName == Totem::Data::NodeName::GPUNode1) {
        return Pin::GPIO5;
    }
    return Pin::GPIO1;
}

[[nodiscard]] constexpr Pin gpuAttentionPin() {
    if constexpr (gpuNodeName == Totem::Data::NodeName::GPUNode1) {
        return Pin::GPIO6;
    }
    return Pin::GPIO2;
}

[[nodiscard]] constexpr Pin ledDataPin() {
    if constexpr (gpuNodeName == Totem::Data::NodeName::GPUNode1) {
        return Pin::GPIO13;
    }
    return Pin::GPIO8;
}

[[nodiscard]] constexpr Pin ledClockPin() {
    if constexpr (gpuNodeName == Totem::Data::NodeName::GPUNode1) {
        return Pin::GPIO10;
    }
    return Pin::GPIO7;
}

[[nodiscard]] constexpr Pin ledPresentStrobePin() {
    if constexpr (gpuNodeName == Totem::Data::NodeName::GPUNode1) {
        return Pin::GPIO4;
    }
    return Pin::GPIO10;
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

inline constexpr Totem::LedDisplay::Config ledDisplayConfig{
    .globalBrightness = 255,
    .temporalDithering = false,
    .sk9822 =
        {
            .host = Totem::LedDisplay::Sk9822SpiHost::Spi3,
            .dataPin = ledDataPin(),
            .clockPin = ledClockPin(),
            .clockHz = 4'000'000,
            .transferTimeoutMs = 10,
            // Candidate order until the BTF strip primary-color bench test.
            .colorOrder = Totem::LedDisplay::Sk9822WireColorOrder::Bgr,
        },
};

inline constexpr Pin ledPresentStrobeInputPin = ledPresentStrobePin();
inline constexpr GpioPull ledPresentStrobeInputPull = GpioPull::Down;

inline constexpr bool ownsLedOutputGate =
    gpuNodeName == Totem::Data::NodeName::GPUNode1;
inline constexpr Pin ledOutputGatePin = Pin::GPIO9;
