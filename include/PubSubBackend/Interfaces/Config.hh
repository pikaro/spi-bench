#pragma once

#include "TaskController/Interfaces/Config.hh"
namespace Totem::PubSubBackend {

struct Config {
    Totem::TaskController::Config task{
        .name = "PubSubNode",
        .stackSize = 8192,
        .intervalMs = 10,
    };

    [[nodiscard]] bool validate() const { return task.validate(); }
};

} // namespace Totem::PubSubBackend
