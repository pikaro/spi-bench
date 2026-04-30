#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
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
        .level = Totem::MetricsBackend::MetricLevel::Profiling,
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

    static constexpr MetricDesc spiTxDef = {
        .name = "spiTx",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc spiAckDef = {
        .name = "spiAck",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc spiFailDef = {
        .name = "spiFail",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc spiRxDef = {
        .name = "spiRx",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc spiDropDef = {
        .name = "spiDrop",
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
        REGISTER_METRIC("PubSub", spiTx, Counter, group);
        REGISTER_METRIC("PubSub", spiAck, Counter, group);
        REGISTER_METRIC("PubSub", spiFail, Counter, group);
        REGISTER_METRIC("PubSub", spiRx, Counter, group);
        REGISTER_METRIC("PubSub", spiDrop, Counter, group);

        return Metrics{
            .group = group,
            .ingressEvictedNoncritical = ingressEvictedNoncritical,
            .ingressDroppedNoncritical = ingressDroppedNoncritical,
            .ingressRejectedCritical = ingressRejectedCritical,
            .egressEvictedNoncritical = egressEvictedNoncritical,
            .egressDroppedNoncritical = egressDroppedNoncritical,
            .egressRejectedCritical = egressRejectedCritical,
            .spiTx = spiTx,
            .spiAck = spiAck,
            .spiFail = spiFail,
            .spiRx = spiRx,
            .spiDrop = spiDrop,
        };
    }

    void addIngressEvictedNoncritical(size_t count = 1) const {
        METRIC_INCR(group, ingressEvictedNoncritical, count);
    }

    void addIngressDroppedNoncritical(size_t count = 1) const {
        METRIC_INCR(group, ingressDroppedNoncritical, count);
    }

    void addIngressRejectedCritical(size_t count = 1) const {
        METRIC_INCR(group, ingressRejectedCritical, count);
    }

    void addEgressEvictedNoncritical(size_t count = 1) const {
        METRIC_INCR(group, egressEvictedNoncritical, count);
    }

    void addEgressDroppedNoncritical(size_t count = 1) const {
        METRIC_INCR(group, egressDroppedNoncritical, count);
    }

    void addEgressRejectedCritical(size_t count = 1) const {
        METRIC_INCR(group, egressRejectedCritical, count);
    }

    void addSpiTx(size_t count = 1) const { METRIC_INCR(group, spiTx, count); }

    void addSpiAck(size_t count = 1) const {
        METRIC_INCR(group, spiAck, count);
    }

    void addSpiFail(size_t count = 1) const {
        METRIC_INCR(group, spiFail, count);
    }

    void addSpiRx(size_t count = 1) const { METRIC_INCR(group, spiRx, count); }

    void addSpiDrop(size_t count = 1) const {
        METRIC_INCR(group, spiDrop, count);
    }

    GroupHandle group;
    CounterHandle ingressEvictedNoncritical;
    CounterHandle ingressDroppedNoncritical;
    CounterHandle ingressRejectedCritical;
    CounterHandle egressEvictedNoncritical;
    CounterHandle egressDroppedNoncritical;
    CounterHandle egressRejectedCritical;
    CounterHandle spiTx;
    CounterHandle spiAck;
    CounterHandle spiFail;
    CounterHandle spiRx;
    CounterHandle spiDrop;

    static constexpr auto component = MetricsBackend::MetricComponent::PubSub;
};

inline Metrics &metrics() {
    static Metrics instance = Metrics::create();
    return instance;
}

} // namespace Totem::PubSubBackend::detail
