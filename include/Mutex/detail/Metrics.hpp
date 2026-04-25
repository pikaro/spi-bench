#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp"
#include "Types/Error.hpp"

namespace Totem::Mutex::detail {

struct Metrics {
    static constexpr MetricsBackend::MetricGroupDesc groupDef = {
        .name = "mutex",
        .logLevel = LogLevel::Info,
    };

    static constexpr MetricsBackend::MetricDesc timeoutsCounterDef = {
        .name = "ingEvict",
        .type = MetricsBackend::MetricType::Counter,
        .unit = MetricsBackend::MetricUnit::None,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("Mutex", group);
        REGISTER_METRIC("Mutex", timeoutsCounter, Counter, group);

        return Metrics{
            .group = group,
            .timeoutsCounter = timeoutsCounter,
        };
    }

    ReturnCode timeout() const {
        return ::MetricsService::recorder().increment(timeoutsCounter);
    }

    Totem::MetricsBackend::GroupHandle group;
    Totem::MetricsBackend::CounterHandle timeoutsCounter;
};

inline Metrics &metrics() {
    static Metrics instance = Metrics::create();
    return instance;
}

} // namespace Totem::Mutex::detail
