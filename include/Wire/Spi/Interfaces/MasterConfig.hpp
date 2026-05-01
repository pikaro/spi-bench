#pragma once

#include "Platform/PlatformSelect.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "Wire/Spi/Interfaces/Types.hpp"
#include <cstdint>
#include <optional>

namespace Totem::Wire::Spi {

struct MasterBusConfig {
    BusId busId = BusId::Bus2;
    BusPins pins{};
    uint32_t maxTransferSize = 4096;

    [[nodiscard]] bool validate() const {
        return pins.validate() && maxTransferSize > 0;
    }
};

struct MasterDeviceConfig {
    std::optional<Pin> csPin = std::nullopt;
    uint32_t clockHz = 5'000'000;
    Mode mode = Mode::Mode0;
    BitOrder bitOrder = BitOrder::MsbFirst;
    uint8_t queueSize = 1;
    int inputDelayNs = 0;

    [[nodiscard]] bool validate() const {
        return csPin.has_value() && clockHz > 0 && queueSize > 0;
    }
};

struct MasterConfig {
    MasterBusConfig bus{};
    MasterDeviceConfig device{};
    Totem::TaskController::Config task{
        .name = "SpiMasterTask",
        .priority = 2,
        .stackSize = 8192,
        .intervalMs = 100,
        .noCatchup = true,
        .useNotify = true,
        .notifyExpectTimeout = false,
        .notifyTimeoutMs = 10,
    };
    uint32_t serviceBudgetMs = 0;
    uint8_t maxTurnsPerStep = 1;
    uint32_t interTurnDelayMs = 0;
    uint32_t maxOutboundSlotBytes = 512;
    uint32_t attentionReceiveWindowBytes = 512;
    bool heartbeatEnabled = false;
    std::optional<Pin> attentionPin = std::nullopt;

    [[nodiscard]] bool validate() const {
        return bus.validate() && device.validate() && task.validate() &&
               maxTurnsPerStep > 0 && maxOutboundSlotBytes > 0 &&
               maxOutboundSlotBytes <= bus.maxTransferSize &&
               attentionReceiveWindowBytes > 0 &&
               attentionReceiveWindowBytes <= bus.maxTransferSize;
    }
};

} // namespace Totem::Wire::Spi
