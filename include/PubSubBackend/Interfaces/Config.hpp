#pragma once

#include "TaskController/Interfaces/Config.hpp"
namespace Totem::PubSubBackend {

struct Config {
    Totem::TaskController::Config task{
        .name = "PubSubNode",
        .stackSize = 9000,
        .intervalMs = 100,
        .noCatchup = true,
        .useNotify = true,
        .notifyTimeoutMs = 10,
    };

    [[nodiscard]] bool validate() const { return task.validate(); }
};

} // namespace Totem::PubSubBackend
