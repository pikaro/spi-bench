#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include "StaticConfig/Metrics.hpp"
#include <cstddef>

namespace Totem::PubSubBackend::detail {

struct Metrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;
    using MetricUnit = Totem::MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc groupDef = {
        .name = "pubsub",
        .logLevel = LogLevel::Info,
    };

    static constexpr MetricDesc ingressEvictedNoncriticalDef = {
        .name = "ingEvict",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc ingressDroppedNoncriticalDef = {
        .name = "ingDropd",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc ingressRejectedCriticalDef = {
        .name = "ingRejec",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc egressEvictedNoncriticalDef = {
        .name = "egrEvict",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc egressDroppedNoncriticalDef = {
        .name = "egrDropd",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc egressRejectedCriticalDef = {
        .name = "egrRejec",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("PubSub", group);
        REGISTER_METRIC("PubSub", ingressEvictedNoncritical, Counter, group);
        REGISTER_METRIC("PubSub", ingressDroppedNoncritical, Counter, group);
        REGISTER_METRIC("PubSub", ingressRejectedCritical, Counter, group);
        REGISTER_METRIC("PubSub", egressEvictedNoncritical, Counter, group);
        REGISTER_METRIC("PubSub", egressDroppedNoncritical, Counter, group);
        REGISTER_METRIC("PubSub", egressRejectedCritical, Counter, group);

        return Metrics{
            .group = group,
            .ingressEvictedNoncritical = ingressEvictedNoncritical,
            .ingressDroppedNoncritical = ingressDroppedNoncritical,
            .ingressRejectedCritical = ingressRejectedCritical,
            .egressEvictedNoncritical = egressEvictedNoncritical,
            .egressDroppedNoncritical = egressDroppedNoncritical,
            .egressRejectedCritical = egressRejectedCritical,
        };
    }

    void addIngressEvictedNoncritical(size_t count = 1) const {
        METRIC_INCR(ingressEvictedNoncritical, count);
    }

    void addIngressDroppedNoncritical(size_t count = 1) const {
        METRIC_INCR(ingressDroppedNoncritical, count);
    }

    void addIngressRejectedCritical(size_t count = 1) const {
        METRIC_INCR(ingressRejectedCritical, count);
    }

    void addEgressEvictedNoncritical(size_t count = 1) const {
        METRIC_INCR(egressEvictedNoncritical, count);
    }

    void addEgressDroppedNoncritical(size_t count = 1) const {
        METRIC_INCR(egressDroppedNoncritical, count);
    }

    void addEgressRejectedCritical(size_t count = 1) const {
        METRIC_INCR(egressRejectedCritical, count);
    }

    GroupHandle group;
    CounterHandle ingressEvictedNoncritical;
    CounterHandle ingressDroppedNoncritical;
    CounterHandle ingressRejectedCritical;
    CounterHandle egressEvictedNoncritical;
    CounterHandle egressDroppedNoncritical;
    CounterHandle egressRejectedCritical;

    static constexpr LogLevel minimumLogLevel = MetricCollection::pubSub;
    static constexpr bool enabled =
        metrics_enabled(groupDef.logLevel, minimumLogLevel);
};

inline Metrics &metrics() {
    static Metrics instance = Metrics::create();
    return instance;
}

} // namespace Totem::PubSubBackend::detail
