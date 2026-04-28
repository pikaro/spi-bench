#pragma once

#include <cstddef>

struct Tracing {
    static constexpr bool enabled = false;
    static constexpr bool rs485 = true;
    static constexpr bool pubSub = true;
};

static constexpr bool tracing_for(bool component) {
    return Tracing::enabled && component;
}

struct LoggingConfig {
    static constexpr std::size_t maxSinks = 3;
    static constexpr bool useColor = true;
};
