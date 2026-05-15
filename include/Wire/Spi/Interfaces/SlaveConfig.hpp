#pragma once

#include "Platform/PlatformSelect.hpp"
#include "StaticConfig/Stacks.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "Wire/Spi/Interfaces/Types.hpp"
#include <cstdint>
#include <optional>

namespace Totem::Wire::Spi {

struct SlaveConfig {
    BusId busId = BusId::Bus2;
    BusPins pins{};
    std::optional<Pin> csPin = std::nullopt;
    uint32_t maxTransferSize = 4096;
    uint32_t transferWindowBytes = 512;
    uint32_t maxOutboundSlotBytes = 512;
    Mode mode = Mode::Mode0;
    BitOrder bitOrder = BitOrder::MsbFirst;
    uint8_t queueSize = 1;
    Totem::TaskController::Config task{
        .name = "SpiSlaveTask",
        .priority = 2,
        .stackSize = StaticConfig::TaskStacks::spiSlave,
        .intervalMs = 100,
        .noCatchup = true,
        .useNotify = true,
        .notifyExpectTimeout = false,
        .notifyTimeoutMs = 10,
    };
    std::optional<Pin> attentionPin = std::nullopt;

    [[nodiscard]] bool validate() const {
        return pins.validate() && csPin.has_value() && maxTransferSize > 0 &&
               transferWindowBytes > 0 &&
               transferWindowBytes <= maxTransferSize && queueSize > 0 &&
               maxOutboundSlotBytes > 0 &&
               maxOutboundSlotBytes <= transferWindowBytes && task.validate();
    }
};

} // namespace Totem::Wire::Spi
