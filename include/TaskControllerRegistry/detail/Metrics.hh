#pragma once

#include "Macros/Facade.hh"
#include "Services/Metrics.hh"
#include "Types/Error.hh"
#include "Types/Logging.hh"
#include "Types/Metrics.hh"
#include <cstddef>

namespace Totem::TaskControllerRegistry::detail {

struct Metrics {
    static constexpr MetricGroupDesc groupDef = {
        .name = "taskRegi",
        .logLevel = LogLevel::Info,
    };

    static constexpr MetricDesc controllerCountDef = {
        .name = "ctrlCont",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc reapedCountDef = {
        .name = "reaped",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
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
        return ::Metrics::recorder().increment(controllerCount);
    }

    ReturnCode removeTask() const {
        return ::Metrics::recorder().decrement(controllerCount);
    }

    ReturnCode addReaped(size_t count) const {
        return ::Metrics::recorder().increment(reapedCount, count);
    }

    Totem::MetricsBackend::GroupHandle group;
    Totem::MetricsBackend::CounterHandle controllerCount;
    Totem::MetricsBackend::CounterHandle reapedCount;
};

} // namespace Totem::TaskControllerRegistry::detail
