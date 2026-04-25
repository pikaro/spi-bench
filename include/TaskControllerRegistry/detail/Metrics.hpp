#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp"
#include "Types/Error.hpp"
#include <cstddef>

namespace Totem::TaskControllerRegistry::detail {

struct Metrics {
    static constexpr MetricsBackend::MetricGroupDesc groupDef = {
        .name = "taskRegi",
        .logLevel = LogLevel::Info,
    };

    static constexpr MetricsBackend::MetricDesc controllerCountDef = {
        .name = "ctrlCont",
        .type = MetricsBackend::MetricType::Counter,
        .unit = MetricsBackend::MetricUnit::None,
    };

    static constexpr MetricsBackend::MetricDesc reapedCountDef = {
        .name = "reaped",
        .type = MetricsBackend::MetricType::Counter,
        .unit = MetricsBackend::MetricUnit::None,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("TaskControllerRegistry", group);
        REGISTER_METRIC("TaskControllerRegistry", controllerCount, Counter,
                        group);
        REGISTER_METRIC("TaskControllerRegistry", reapedCount, Counter, group);

        return Metrics{
            .group = group,
            .controllerCount = controllerCount,
            .reapedCount = reapedCount,
        };
    }

    ReturnCode addTask() const {
        return ::MetricsService::recorder().increment(controllerCount);
    }

    ReturnCode removeTask() const {
        return ::MetricsService::recorder().decrement(controllerCount);
    }

    ReturnCode addReaped(size_t count) const {
        return ::MetricsService::recorder().increment(reapedCount, count);
    }

    Totem::MetricsBackend::GroupHandle group;
    Totem::MetricsBackend::CounterHandle controllerCount;
    Totem::MetricsBackend::CounterHandle reapedCount;
};

} // namespace Totem::TaskControllerRegistry::detail
