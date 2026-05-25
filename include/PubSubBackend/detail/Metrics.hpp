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

    static constexpr MetricGroupDesc udpGroupDef = {
        .name = "psUdp",
        .level = Totem::MetricsBackend::MetricLevel::Baseline,
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

    static constexpr MetricDesc udpPeerDef = {
        .name = "udpPeer",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc udpLostDef = {
        .name = "udpLost",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc udpAvailDef = {
        .name = "udpAvail",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc udpTxDef = {
        .name = "udpTx",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc udpRxDef = {
        .name = "udpRx",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc udpTxBDef = {
        .name = "udpTxB",
        .type = MetricType::Counter,
        .unit = MetricUnit::Bytes,
    };

    static constexpr MetricDesc udpRxBDef = {
        .name = "udpRxB",
        .type = MetricType::Counter,
        .unit = MetricUnit::Bytes,
    };

    static constexpr MetricDesc udpFailDef = {
        .name = "udpFail",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc udpDropDef = {
        .name = "udpDrop",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc udpBadDef = {
        .name = "udpBad",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc udpOtherDef = {
        .name = "udpOther",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc udpNoPeerDef = {
        .name = "udpNoPeer",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc udpKeepTxDef = {
        .name = "udpKTx",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc udpKeepRxDef = {
        .name = "udpKRx",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc udpControlDef = {
        .name = "udpCtrl",
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

        REGISTER_METRICS_GROUP("PubSubUdp", udpGroup);
        REGISTER_METRIC("PubSubUdp", udpPeer, Counter, udpGroup);
        REGISTER_METRIC("PubSubUdp", udpLost, Counter, udpGroup);
        REGISTER_METRIC("PubSubUdp", udpAvail, Counter, udpGroup);
        REGISTER_METRIC("PubSubUdp", udpTx, Counter, udpGroup);
        REGISTER_METRIC("PubSubUdp", udpRx, Counter, udpGroup);
        REGISTER_METRIC("PubSubUdp", udpTxB, Counter, udpGroup);
        REGISTER_METRIC("PubSubUdp", udpRxB, Counter, udpGroup);
        REGISTER_METRIC("PubSubUdp", udpFail, Counter, udpGroup);
        REGISTER_METRIC("PubSubUdp", udpDrop, Counter, udpGroup);
        REGISTER_METRIC("PubSubUdp", udpBad, Counter, udpGroup);
        REGISTER_METRIC("PubSubUdp", udpOther, Counter, udpGroup);
        REGISTER_METRIC("PubSubUdp", udpNoPeer, Counter, udpGroup);
        REGISTER_METRIC("PubSubUdp", udpKeepTx, Counter, udpGroup);
        REGISTER_METRIC("PubSubUdp", udpKeepRx, Counter, udpGroup);
        REGISTER_METRIC("PubSubUdp", udpControl, Counter, udpGroup);

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
            .udpGroup = udpGroup,
            .udpPeer = udpPeer,
            .udpLost = udpLost,
            .udpAvail = udpAvail,
            .udpTx = udpTx,
            .udpRx = udpRx,
            .udpTxB = udpTxB,
            .udpRxB = udpRxB,
            .udpFail = udpFail,
            .udpDrop = udpDrop,
            .udpBad = udpBad,
            .udpOther = udpOther,
            .udpNoPeer = udpNoPeer,
            .udpKeepTx = udpKeepTx,
            .udpKeepRx = udpKeepRx,
            .udpControl = udpControl,
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

    void addUdpPeerLearned(size_t count = 1) const {
        METRIC_INCR(udpGroup, udpPeer, count);
    }

    void addUdpPeerReset(size_t count = 1) const {
        METRIC_INCR(udpGroup, udpLost, count);
    }

    void addUdpAvailabilityChange(size_t count = 1) const {
        METRIC_INCR(udpGroup, udpAvail, count);
    }

    void addUdpTx(size_t count = 1) const {
        METRIC_INCR(udpGroup, udpTx, count);
    }

    void addUdpRx(size_t count = 1) const {
        METRIC_INCR(udpGroup, udpRx, count);
    }

    void addUdpTxBytes(size_t count) const {
        METRIC_INCR(udpGroup, udpTxB, count);
    }

    void addUdpRxBytes(size_t count) const {
        METRIC_INCR(udpGroup, udpRxB, count);
    }

    void addUdpFailure(size_t count = 1) const {
        METRIC_INCR(udpGroup, udpFail, count);
    }

    void addUdpDrop(size_t count = 1) const {
        METRIC_INCR(udpGroup, udpDrop, count);
    }

    void addUdpBadFrame(size_t count = 1) const {
        METRIC_INCR(udpGroup, udpBad, count);
    }

    void addUdpUnexpectedPeer(size_t count = 1) const {
        METRIC_INCR(udpGroup, udpOther, count);
    }

    void addUdpNoPeer(size_t count = 1) const {
        METRIC_INCR(udpGroup, udpNoPeer, count);
    }

    void addUdpKeepaliveTx(size_t count = 1) const {
        METRIC_INCR(udpGroup, udpKeepTx, count);
    }

    void addUdpKeepaliveRx(size_t count = 1) const {
        METRIC_INCR(udpGroup, udpKeepRx, count);
    }

    void addUdpControlFrame(size_t count = 1) const {
        METRIC_INCR(udpGroup, udpControl, count);
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
    GroupHandle udpGroup;
    CounterHandle udpPeer;
    CounterHandle udpLost;
    CounterHandle udpAvail;
    CounterHandle udpTx;
    CounterHandle udpRx;
    CounterHandle udpTxB;
    CounterHandle udpRxB;
    CounterHandle udpFail;
    CounterHandle udpDrop;
    CounterHandle udpBad;
    CounterHandle udpOther;
    CounterHandle udpNoPeer;
    CounterHandle udpKeepTx;
    CounterHandle udpKeepRx;
    CounterHandle udpControl;

    static constexpr auto component = MetricsBackend::MetricComponent::PubSub;
};

DEFINE_PREWARMED_METRICS_ACCESSORS(Metrics, metrics, prewarmMetrics)

} // namespace Totem::PubSubBackend::detail
