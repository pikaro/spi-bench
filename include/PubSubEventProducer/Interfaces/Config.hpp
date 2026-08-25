#pragma once

#include "StaticConfig/Stacks.hpp"
#include "TaskController/Interfaces/Config.hpp"

namespace Totem::PubSubEventProducer {

struct Config {
    Totem::TaskController::Config task{
        .name = "EventProducer",
        .stackSize = StaticConfig::TaskStacks::pubSubEventProducer,
        .intervalMs = 100,
        .useNotify = true,
    };

    [[nodiscard]] bool validate() const { return task.validate(); }
};

} // namespace Totem::PubSubEventProducer
