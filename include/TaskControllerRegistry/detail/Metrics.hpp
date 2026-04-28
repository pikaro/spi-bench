#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include "StaticConfig/Metrics.hpp"
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

    void addTask() const { METRIC_INCR(controllerCount, 1); }
    void removeTask() const { METRIC_DECR(controllerCount, 1); }
    void addReaped(size_t count) const { METRIC_INCR(reapedCount, count); }

    Totem::MetricsBackend::GroupHandle group;
    Totem::MetricsBackend::CounterHandle controllerCount;
    Totem::MetricsBackend::CounterHandle reapedCount;

    static constexpr bool enabled =
        MetricCollection::enabled && MetricCollection::taskControllerRegistry;
};

} // namespace Totem::TaskControllerRegistry::detail
