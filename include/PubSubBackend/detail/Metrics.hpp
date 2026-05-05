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

    static constexpr MetricGroupDesc coreGroupDef = {
        .name = "psCore",
        .level = Totem::MetricsBackend::MetricLevel::Core,
    };

    static constexpr MetricGroupDesc spiGroupDef = {
        .name = "psSpi",
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
        REGISTER_METRICS_GROUP("PubSubCore", coreGroup);
        REGISTER_METRIC("PubSubCore", ingressEvictedNoncritical, Counter,
                        coreGroup);
        REGISTER_METRIC("PubSubCore", ingressDroppedNoncritical, Counter,
                        coreGroup);
        REGISTER_METRIC("PubSubCore", ingressRejectedCritical, Counter,
                        coreGroup);
        REGISTER_METRIC("PubSubCore", egressEvictedNoncritical, Counter,
                        coreGroup);
        REGISTER_METRIC("PubSubCore", egressDroppedNoncritical, Counter,
                        coreGroup);
        REGISTER_METRIC("PubSubCore", egressRejectedCritical, Counter,
                        coreGroup);
        REGISTER_METRIC("PubSubCore", spiFail, Counter, coreGroup);
        REGISTER_METRIC("PubSubCore", spiDrop, Counter, coreGroup);

        REGISTER_METRICS_GROUP("PubSubSpi", spiGroup);
        REGISTER_METRIC("PubSubSpi", spiTx, Counter, spiGroup);
        REGISTER_METRIC("PubSubSpi", spiAck, Counter, spiGroup);
        REGISTER_METRIC("PubSubSpi", spiRx, Counter, spiGroup);

        return Metrics{
            .coreGroup = coreGroup,
            .ingressEvictedNoncritical = ingressEvictedNoncritical,
            .ingressDroppedNoncritical = ingressDroppedNoncritical,
            .ingressRejectedCritical = ingressRejectedCritical,
            .egressEvictedNoncritical = egressEvictedNoncritical,
            .egressDroppedNoncritical = egressDroppedNoncritical,
            .egressRejectedCritical = egressRejectedCritical,
            .spiFail = spiFail,
            .spiDrop = spiDrop,
            .spiGroup = spiGroup,
            .spiTx = spiTx,
            .spiAck = spiAck,
            .spiRx = spiRx,
        };
    }

    void addIngressEvictedNoncritical(size_t count = 1) const {
        METRIC_INCR(coreGroup, ingressEvictedNoncritical, count);
    }

    void addIngressDroppedNoncritical(size_t count = 1) const {
        METRIC_INCR(coreGroup, ingressDroppedNoncritical, count);
    }

    void addIngressRejectedCritical(size_t count = 1) const {
        METRIC_INCR(coreGroup, ingressRejectedCritical, count);
    }

    void addEgressEvictedNoncritical(size_t count = 1) const {
        METRIC_INCR(coreGroup, egressEvictedNoncritical, count);
    }

    void addEgressDroppedNoncritical(size_t count = 1) const {
        METRIC_INCR(coreGroup, egressDroppedNoncritical, count);
    }

    void addEgressRejectedCritical(size_t count = 1) const {
        METRIC_INCR(coreGroup, egressRejectedCritical, count);
    }

    void addSpiTx(size_t count = 1) const {
        METRIC_INCR(spiGroup, spiTx, count);
    }

    void addSpiAck(size_t count = 1) const {
        METRIC_INCR(spiGroup, spiAck, count);
    }

    void addSpiFail(size_t count = 1) const {
        METRIC_INCR(coreGroup, spiFail, count);
    }

    void addSpiRx(size_t count = 1) const {
        METRIC_INCR(spiGroup, spiRx, count);
    }

    void addSpiDrop(size_t count = 1) const {
        METRIC_INCR(coreGroup, spiDrop, count);
    }

    GroupHandle coreGroup;
    CounterHandle ingressEvictedNoncritical;
    CounterHandle ingressDroppedNoncritical;
    CounterHandle ingressRejectedCritical;
    CounterHandle egressEvictedNoncritical;
    CounterHandle egressDroppedNoncritical;
    CounterHandle egressRejectedCritical;
    CounterHandle spiFail;
    CounterHandle spiDrop;
    GroupHandle spiGroup;
    CounterHandle spiTx;
    CounterHandle spiAck;
    CounterHandle spiRx;

    static constexpr auto component = MetricsBackend::MetricComponent::PubSub;
};

inline Metrics &metrics() {
    static Metrics instance = Metrics::create();
    return instance;
}

} // namespace Totem::PubSubBackend::detail
