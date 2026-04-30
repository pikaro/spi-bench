#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include <cstddef>
#include <cstdint>

struct MetricCollection {
    static constexpr LogLevel minimum = LogLevel::Verbose;
    static constexpr LogLevel pubSub = LogLevel::Off;
    static constexpr LogLevel rs485 = LogLevel::Off;
    static constexpr LogLevel spi = LogLevel::Verbose;
    static constexpr LogLevel taskController = LogLevel::Info;
    static constexpr LogLevel taskControllerRegistry = LogLevel::Info;
    static constexpr LogLevel logging = LogLevel::Info;
    static constexpr LogLevel mutex = LogLevel::Info;
};

static constexpr bool metrics_enabled(LogLevel level,
                                      LogLevel componentMinimum) {
    return static_cast<uint8_t>(level) >=
               static_cast<uint8_t>(MetricCollection::minimum) &&
           static_cast<uint8_t>(level) >=
               static_cast<uint8_t>(componentMinimum);
}

struct MetricConfig {
    static constexpr size_t maxMetrics = 255;
    static constexpr size_t maxMetricGroups = 32;
    static constexpr size_t maxMetricNameLength = 8;
    static constexpr size_t maxMetricGroupNameLength = 8;
    static constexpr size_t maxMetricsPerGroup = 32;

    [[nodiscard]] static bool validate() { return true; }
};
