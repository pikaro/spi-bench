#pragma once

#include "StaticConfig/Stacks.hpp"
#include "TaskController/Interfaces/Config.hpp"

namespace Totem::CommandBackend {

struct Config {
    Totem::TaskController::Config task{
        .name = "Command",
        .stackSize = StaticConfig::TaskStacks::command,
        .intervalMs = 1000,
        .noCatchup = true,
        .useNotify = true,
    };

    [[nodiscard]] bool validate() const { return task.validate(); }
};

} // namespace Totem::CommandBackend
