#pragma once

#include "TaskController/Interfaces/Config.hpp"
#include "Platform/Hardware.hpp"
#include "Types/Uart.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Totem::Wire::Rs485::detail {

struct MasterConfig {
    UartConfig uartConfig;
    Totem::TaskController::Config task{
        .name = "Rs485MasterTask",
        .priority = 2,
        .stackSize = 4096,
        .intervalMs = 10,
        .noCatchup = true,
        .useNotify = true,
        .notifyExpectTimeout = false,
        .notifyTimeoutMs = 10,
    };
    std::optional<::platform::Pin> attentionPin = std::nullopt;
    size_t maxPayloadSize = 128;
    uint8_t handlerSlots = 4;

    [[nodiscard]] bool validate() const {
        return uartConfig.validate() && task.validate() && maxPayloadSize > 0 &&
               maxPayloadSize <= UINT16_MAX && handlerSlots > 0;
    }
};

struct SlaveConfig {
    UartConfig uartConfig;
    Totem::TaskController::Config task{
        .name = "Rs485SlaveTask",
        .priority = 2,
        .stackSize = 4096,
        .intervalMs = 10,
        .noCatchup = true,
        .useNotify = true,
        .notifyExpectTimeout = false,
        .notifyTimeoutMs = 10,
    };
    std::optional<::platform::Pin> attentionPin = std::nullopt;
    size_t maxPayloadSize = 128;
    uint8_t handlerSlots = 4;

    [[nodiscard]] bool validate() const {
        return uartConfig.validate() && task.validate() && maxPayloadSize > 0 &&
               maxPayloadSize <= UINT16_MAX && handlerSlots > 0;
    }
};

} // namespace Totem::Wire::Rs485::detail
