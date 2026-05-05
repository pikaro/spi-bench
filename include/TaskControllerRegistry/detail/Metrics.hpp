#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include <cstddef>
#include <cstdint>

namespace Totem::TaskControllerRegistry::detail {

struct Metrics {
    static constexpr MetricsBackend::MetricGroupDesc groupDef = {
        .name = "taskRegi",
        .level = MetricsBackend::MetricLevel::Baseline,
    };

    static constexpr MetricsBackend::MetricDesc controllerCountDef = {
        .name = "ctrlCont",
        .type = MetricsBackend::MetricType::Gauge,
        .unit = MetricsBackend::MetricUnit::None,
    };

    static constexpr MetricsBackend::MetricDesc reapedCountDef = {
        .name = "reaped",
        .type = MetricsBackend::MetricType::Counter,
        .unit = MetricsBackend::MetricUnit::None,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("TaskControllerRegistry", group);
        REGISTER_METRIC("TaskControllerRegistry", controllerCount, Gauge,
                        group);
        REGISTER_METRIC("TaskControllerRegistry", reapedCount, Counter, group);

        return Metrics{
            .group = group,
            .controllerCount = controllerCount,
            .reapedCount = reapedCount,
        };
    }

    void addTask() {
        ++controllerCountValue;
        METRIC_SET(group, controllerCount, controllerCountValue);
    }
    void removeTask() {
        if (controllerCountValue > 0) {
            --controllerCountValue;
        }
        METRIC_SET(group, controllerCount, controllerCountValue);
    }
    void addReaped(size_t count) const {
        METRIC_INCR(group, reapedCount, count);
    }

    Totem::MetricsBackend::GroupHandle group;
    Totem::MetricsBackend::GaugeHandle controllerCount;
    Totem::MetricsBackend::CounterHandle reapedCount;
    uint32_t controllerCountValue = 0;

    static constexpr auto component =
        MetricsBackend::MetricComponent::TaskControllerRegistry;
};

} // namespace Totem::TaskControllerRegistry::detail
