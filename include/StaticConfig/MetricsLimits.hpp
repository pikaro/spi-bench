#pragma once

#include <cstddef>

/** Platform-independent fixed capacities shared by metric producers. */
struct MetricLimits {
    static constexpr std::size_t maxMetrics = 255;
    static constexpr std::size_t maxMetricGroups = 32;
    static constexpr std::size_t maxMetricNameLength = 8;
    static constexpr std::size_t maxMetricGroupNameLength = 8;
    static constexpr std::size_t maxMetricsPerGroup = 32;
};
