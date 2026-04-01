#pragma once

#include <cstddef>

struct MetricConfig {
    static constexpr size_t maxMetrics = 255;
    static constexpr size_t maxMetricGroups = 32;
    static constexpr size_t maxMetricNameLength = 8;
    static constexpr size_t maxMetricGroupNameLength = 8;
    static constexpr size_t maxMetricsPerGroup = 8;

    [[nodiscard]] static bool validate() { return true; }
};
