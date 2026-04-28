#pragma once

#include <cstddef>

struct MetricCollection {
    static constexpr bool enabled = true;
    static constexpr bool pubSub = false;
    static constexpr bool rs485 = false;
    static constexpr bool taskController = true;
    static constexpr bool taskControllerRegistry = true;
    static constexpr bool logging = true;
    static constexpr bool mutex = true;
};

struct MetricConfig {
    static constexpr size_t maxMetrics = 255;
    static constexpr size_t maxMetricGroups = 32;
    static constexpr size_t maxMetricNameLength = 8;
    static constexpr size_t maxMetricGroupNameLength = 8;
    static constexpr size_t maxMetricsPerGroup = 32;

    [[nodiscard]] static bool validate() { return true; }
};
