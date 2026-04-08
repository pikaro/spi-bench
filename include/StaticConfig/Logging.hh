#pragma once

#include "TaskController/Interfaces/Config.hh"
#include <cstddef>

struct LoggingConfig {
    static constexpr std::size_t maxSinks = 3;
    static constexpr bool useColor = true;

    static constexpr Totem::TaskController::Config task{
        .name = "Output",
        .stackSize = 4096,
        .intervalMs = 10,
    };
};
