#pragma once

#include "TaskController/Interfaces/Config.hh"
#include <cstddef>

struct LoggingConfig {
    static constexpr std::size_t maxSinks = 3;
    static constexpr bool useColor = true;
};
