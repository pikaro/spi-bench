#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep

namespace Totem::TaskController::detail {

struct TaskMetrics {
    static constexpr MetricsBackend::MetricGroupDesc baseDef = {
        .name = "undefined",
        .level = Totem::MetricsBackend::MetricLevel::Baseline,
    };

    static constexpr MetricsBackend::MetricDesc startedCountDef = {
        .name = "started",
        .type = MetricsBackend::MetricType::Counter,
        .unit = MetricsBackend::MetricUnit::None,
    };

    static constexpr MetricsBackend::MetricDesc stoppedCountDef = {
        .name = "stopped",
        .type = MetricsBackend::MetricType::Counter,
        .unit = MetricsBackend::MetricUnit::None,
    };

    static TaskMetrics create(const MetricsBackend::MetricGroupDesc &groupDef) {
        REGISTER_METRICS_GROUP("Runner", group);
        REGISTER_METRIC("Runner", startedCount, Counter, group);
        REGISTER_METRIC("Runner", stoppedCount, Counter, group);

        return TaskMetrics{
            .group = group,
            .startedCount = startedCount,
            .stoppedCount = stoppedCount,
        };
    }

    void started() const { METRIC_INCR(base, startedCount, 1); }
    void stopped() const { METRIC_INCR(base, stoppedCount, 1); }

    Totem::MetricsBackend::GroupHandle group;
    Totem::MetricsBackend::CounterHandle startedCount;
    Totem::MetricsBackend::CounterHandle stoppedCount;

    static constexpr auto component =
        MetricsBackend::MetricComponent::TaskController;
};

} // namespace Totem::TaskController::detail
