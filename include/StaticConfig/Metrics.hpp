#pragma once

#include "MetricsBackend/Interfaces/Types.hpp"
#include <cstddef>
#include <optional>

#ifndef TOTEM_ENABLE_AUDIO_PROFILING_METRICS
#define TOTEM_ENABLE_AUDIO_PROFILING_METRICS 0
#endif

struct MetricCollection {
    using MetricLevel = Totem::MetricsBackend::MetricLevel;

    // Components set to std::nullopt use this default threshold. Groups at
    // shallower levels are compiled out by metrics_enabled().
    static constexpr MetricLevel minimum = MetricLevel::Baseline;
    static constexpr std::optional<MetricLevel> pubSub = std::nullopt;
    static constexpr std::optional<MetricLevel> rs485 = std::nullopt;
    static constexpr std::optional<MetricLevel> spi = MetricLevel::Profiling;
    static constexpr std::optional<MetricLevel> taskController = std::nullopt;
    static constexpr std::optional<MetricLevel> taskControllerRegistry =
        std::nullopt;
    static constexpr std::optional<MetricLevel> logging = std::nullopt;
    static constexpr std::optional<MetricLevel> mutex = std::nullopt;
#if TOTEM_ENABLE_AUDIO_PROFILING_METRICS
    static constexpr std::optional<MetricLevel> audio = MetricLevel::Profiling;
#else
    static constexpr std::optional<MetricLevel> audio = MetricLevel::Diagnostic;
#endif
    static constexpr std::optional<MetricLevel> i2c = MetricLevel::Diagnostic;
    static constexpr std::optional<MetricLevel> ledPwm = std::nullopt;
    static constexpr std::optional<MetricLevel> buttons = std::nullopt;
    static constexpr std::optional<MetricLevel> bluetooth = std::nullopt;
    static constexpr std::optional<MetricLevel> wheel = std::nullopt;
};

struct MetricConfig {
    static constexpr size_t maxMetrics = 255;
    static constexpr size_t maxMetricGroups = 32;
    static constexpr size_t maxMetricNameLength = 8;
    static constexpr size_t maxMetricGroupNameLength = 8;
    static constexpr size_t maxMetricsPerGroup = 32;

    [[nodiscard]] static bool validate() { return true; }
};
