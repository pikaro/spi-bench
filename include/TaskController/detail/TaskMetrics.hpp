#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include "StaticConfig/Metrics.hpp"

namespace Totem::TaskController::detail {

struct TaskMetrics {
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

    void started() const { METRIC_INCR(startedCount, 1); }
    void stopped() const { METRIC_INCR(stoppedCount, 1); }

    Totem::MetricsBackend::GroupHandle group;
    Totem::MetricsBackend::CounterHandle startedCount;
    Totem::MetricsBackend::CounterHandle stoppedCount;

    static constexpr bool enabled =
        MetricCollection::enabled && MetricCollection::taskController;
};

} // namespace Totem::TaskController::detail
