#pragma once

#include "StaticConfig/Stacks.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include <cstdint>

namespace Totem::PubSubBackend {

struct Config {
    Totem::TaskController::Config task{
        .name = "PubSubNode",
        .stackSize = StaticConfig::TaskStacks::pubSubNode,
        .intervalMs = 100,
        .noCatchup = true,
        .useNotify = true,
        .notifyTimeoutMs = 10,
    };

    // Delay availability-triggered subscription replay until the peer's
    // transport has had time to finish link-ready work.
    uint32_t subscriptionReplayDelayMs = 50;

    // Optional soft-state refresh for subscription control messages. A value
    // of 0 keeps the default one-shot availability replay behavior.
    uint32_t subscriptionReplayIntervalMs = 0;

    [[nodiscard]] bool validate() const { return task.validate(); }
};

} // namespace Totem::PubSubBackend
