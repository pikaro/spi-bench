#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep

namespace Totem::Mutex::detail {

struct Metrics {
    static constexpr MetricsBackend::MetricGroupDesc groupDef = {
        .name = "mutex",
        .level = MetricsBackend::MetricLevel::Baseline,
    };

    static constexpr MetricsBackend::MetricDesc timeoutsCounterDef = {
        .name = "timeouts",
        .type = MetricsBackend::MetricType::Counter,
        .unit = MetricsBackend::MetricUnit::None,
    };

    static constexpr MetricsBackend::MetricDesc failureCounterDef = {
        .name = "failures",
        .type = MetricsBackend::MetricType::Counter,
        .unit = MetricsBackend::MetricUnit::None,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("Mutex", group);
        REGISTER_METRIC("Mutex", timeoutsCounter, Counter, group);
        REGISTER_METRIC("Mutex", failureCounter, Counter, group);

        return Metrics{
            .group = group,
            .timeoutsCounter = timeoutsCounter,
            .failureCounter = failureCounter,
        };
    }

    void timeout() const { METRIC_INCR(group, timeoutsCounter, 1); }
    void failure() const { METRIC_INCR(group, failureCounter, 1); }

    Totem::MetricsBackend::GroupHandle group;
    Totem::MetricsBackend::CounterHandle timeoutsCounter;
    Totem::MetricsBackend::CounterHandle failureCounter;

    static constexpr auto component = MetricsBackend::MetricComponent::Mutex;
};

inline Metrics &metrics() {
    static Metrics instance = Metrics::create();
    return instance;
}

} // namespace Totem::Mutex::detail
