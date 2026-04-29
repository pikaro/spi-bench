#pragma once

#include "Platform/PlatformSelect.hpp"
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
    Mode mode = Mode::Mode0;
    BitOrder bitOrder = BitOrder::MsbFirst;
    uint8_t queueSize = 1;
    Totem::TaskController::Config task{
        .name = "SpiSlaveTask",
        .priority = 2,
        .stackSize = 4096,
        .intervalMs = 100,
        .noCatchup = true,
        .useNotify = true,
        .notifyExpectTimeout = false,
        .notifyTimeoutMs = 10,
    };
    std::optional<Pin> attentionPin = std::nullopt;

    [[nodiscard]] bool validate() const {
        return pins.validate() && csPin.has_value() && maxTransferSize > 0 &&
               queueSize > 0 && task.validate();
    }
};

} // namespace Totem::Wire::Spi
