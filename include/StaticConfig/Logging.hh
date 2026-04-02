#pragma once

#include "TaskController/Facade.hh"
#include <cstddef>

struct LoggingConfig {
    static constexpr std::size_t maxSinks = 3;

    static constexpr Totem::TaskController::Config task{
        .name = "Output",
        .stackSize = 4096,
        .intervalMs = 10,
    };
};
