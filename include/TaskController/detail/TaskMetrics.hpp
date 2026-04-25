#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp"
#include "Types/Error.hpp"

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

    ReturnCode started() const {
        return ::MetricsService::recorder().increment(startedCount);
    }

    ReturnCode stopped() const {
        return ::MetricsService::recorder().increment(stoppedCount);
    }

    Totem::MetricsBackend::GroupHandle group;
    Totem::MetricsBackend::CounterHandle startedCount;
    Totem::MetricsBackend::CounterHandle stoppedCount;
};

} // namespace Totem::TaskController::detail
