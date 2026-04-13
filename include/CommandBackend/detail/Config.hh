#pragma once

#include "TaskController/Interfaces/Config.hh"

namespace Totem::CommandBackend::detail {

struct Config {
    Totem::TaskController::Config task{
        .name = "Command",
        .stackSize = 4096,
        .intervalMs = 10,
    };
};

} // namespace Totem::CommandBackend::detail
