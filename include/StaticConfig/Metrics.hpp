#pragma once

#include "MetricsBackend/Interfaces/Types.hpp"
#include <cstddef>
#include <optional>

struct MetricCollection {
    using MetricLevel = Totem::MetricsBackend::MetricLevel;

    static constexpr MetricLevel minimum = MetricLevel::Baseline;
    static constexpr std::optional<MetricLevel> pubSub = std::nullopt;
    static constexpr std::optional<MetricLevel> rs485 = std::nullopt;
    static constexpr std::optional<MetricLevel> spi = MetricLevel::Profiling;
    static constexpr std::optional<MetricLevel> taskController = std::nullopt;
    static constexpr std::optional<MetricLevel> taskControllerRegistry =
        std::nullopt;
    static constexpr std::optional<MetricLevel> logging = std::nullopt;
    static constexpr std::optional<MetricLevel> mutex = std::nullopt;
};

struct MetricConfig {
    static constexpr size_t maxMetrics = 255;
    static constexpr size_t maxMetricGroups = 32;
    static constexpr size_t maxMetricNameLength = 8;
    static constexpr size_t maxMetricGroupNameLength = 8;
    static constexpr size_t maxMetricsPerGroup = 32;

    [[nodiscard]] static bool validate() { return true; }
};
