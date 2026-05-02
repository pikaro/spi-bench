#pragma once

#include "Platform/Hardware.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "Types/Uart.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Totem::Wire::Rs485 {

struct MasterConfig {
    UartConfig uartConfig;
    Totem::TaskController::Config task{
        .name = "Rs485MasterTask",
        .priority = 2,
        .stackSize = 8192,
        .intervalMs = 100,
        .noCatchup = true,
        .useNotify = true,
        .notifyExpectTimeout = false,
        .notifyTimeoutMs = 10,
    };
    std::optional<Pin> attentionPin = std::nullopt;
    size_t maxPayloadSize = 128;
    uint8_t handlerSlots = 4;

    [[nodiscard]] bool validate() const {
        return uartConfig.validate() && task.validate() && maxPayloadSize > 0 &&
               maxPayloadSize <= UINT16_MAX && handlerSlots > 0;
    }
};

} // namespace Totem::Wire::Rs485
