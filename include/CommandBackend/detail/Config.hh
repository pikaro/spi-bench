#pragma once

#include "TaskController/Interfaces/Config.hh"

namespace Totem::CommandBackend {

struct Config {
    Totem::TaskController::Config task{
        .name = "Command",
        .stackSize = 4096,
        .intervalMs = 10,
    };

    [[nodiscard]] bool validate() const { return task.validate(); }
};

} // namespace Totem::CommandBackend
