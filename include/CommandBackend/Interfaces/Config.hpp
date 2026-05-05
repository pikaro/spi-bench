#pragma once

#include "TaskController/Interfaces/Config.hpp"

namespace Totem::CommandBackend {

struct Config {
    Totem::TaskController::Config task{
        .name = "Command",
        .stackSize = 6144,
        .intervalMs = 1000,
        .noCatchup = true,
        .useNotify = true,
    };

    [[nodiscard]] bool validate() const { return task.validate(); }
};

} // namespace Totem::CommandBackend
