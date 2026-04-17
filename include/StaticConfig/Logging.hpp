#pragma once

#include <cstddef>

struct LoggingConfig {
    static constexpr std::size_t maxSinks = 3;
    static constexpr bool useColor = true;
};
