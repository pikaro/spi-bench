#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include "Types/Error.hpp"
#include <cstdint>

namespace Totem::Wire::Spi::detail {

enum class LinkRecoveryReason : uint8_t {
    Corrupted,
    Crc,
    Sequence,
    Timeout,
    HelloResync,
    Other,
};

[[nodiscard]] inline LinkRecoveryReason
link_recovery_reason_from_error(ReturnCode error) {
    if (error == ERR(WireError, Corrupted)) {
        return LinkRecoveryReason::Corrupted;
    }
    if (error == ERR(WireError, CrcError) ||
        error == ERR(CoreError, CrcError)) {
        return LinkRecoveryReason::Crc;
    }
    if (error == ERR(WireError, SequenceError)) {
        return LinkRecoveryReason::Sequence;
    }
    if (error == ERR(CoreError, Timeout)) {
        return LinkRecoveryReason::Timeout;
    }
    return LinkRecoveryReason::Other;
}

[[nodiscard]] constexpr const char *
link_recovery_reason_name(LinkRecoveryReason reason) {
    switch (reason) {
    case LinkRecoveryReason::Corrupted:
        return "corrupted";
    case LinkRecoveryReason::Crc:
        return "crc";
    case LinkRecoveryReason::Sequence:
        return "sequence";
    case LinkRecoveryReason::Timeout:
        return "timeout";
    case LinkRecoveryReason::HelloResync:
        return "hello-resync";
    case LinkRecoveryReason::Other:
        return "other";
    }
    return "unknown";
}

struct Metrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using GaugeHandle = Totem::MetricsBackend::GaugeHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;
    using MetricUnit = Totem::MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc groupDef = {
        .name = "spi",
        .level = Totem::MetricsBackend::MetricLevel::Profiling,
    };
    static constexpr MetricGroupDesc diagnosticGroupDef = {
        .name = "spiDiag",
        .level = Totem::MetricsBackend::MetricLevel::Diagnostic,
    };
    static constexpr MetricGroupDesc coreGroupDef = {
        .name = "spiCore",
        .level = Totem::MetricsBackend::MetricLevel::Core,
    };
    static constexpr MetricDesc turnsDef = {
        .name = "turns",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc taskStepsDef = {
        .name = "steps",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc attentionWakesDef = {
        .name = "attnWake",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc attentionAssertDef = {
        .name = "attnAss",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc attentionReleaseDef = {
        .name = "attnRel",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc txBytesDef = {
        .name = "txByte",
        .type = MetricType::Counter,
        .unit = MetricUnit::Bytes,
    };
    static constexpr MetricDesc rxBytesDef = {
        .name = "rxByte",
        .type = MetricType::Counter,
        .unit = MetricUnit::Bytes,
    };
    static constexpr MetricDesc slotsRxDef = {
        .name = "slotRx",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc framesRxDef = {
        .name = "frmRx",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc framesTxDef = {
        .name = "frmTx",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc crcErrDef = {
        .name = "crcErr",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc noSlotDef = {
        .name = "noSlot",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc emptyDef = {
        .name = "empty",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc badSlotDef = {
        .name = "badSlot",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc missSeqDef = {
        .name = "missSeq",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc staleSeqDef = {
        .name = "staleSeq",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc helloReDef = {
        .name = "helloRe",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc resetsDef = {
        .name = "reset",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc maxTurnUsDef = {
        .name = "maxTurn",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };
    static constexpr MetricDesc lrCorrDef = {
        .name = "lrCorr",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc lrCrcDef = {
        .name = "lrCrc",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc lrSeqDef = {
        .name = "lrSeq",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc lrTmoDef = {
        .name = "lrTmo",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc lrHelloDef = {
        .name = "lrHello",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc lrOtherDef = {
        .name = "lrOther",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("Spi", group);
        REGISTER_METRIC("Spi", turns, Counter, group);
        REGISTER_METRIC("Spi", taskSteps, Counter, group);
        REGISTER_METRIC("Spi", attentionWakes, Counter, group);
        REGISTER_METRIC("Spi", attentionAssert, Counter, group);
        REGISTER_METRIC("Spi", attentionRelease, Counter, group);
        REGISTER_METRIC("Spi", txBytes, Counter, group);
        REGISTER_METRIC("Spi", rxBytes, Counter, group);
        REGISTER_METRIC("Spi", slotsRx, Counter, group);
        REGISTER_METRIC("Spi", framesRx, Counter, group);
        REGISTER_METRIC("Spi", framesTx, Counter, group);
        REGISTER_METRIC("Spi", empty, Counter, group);
        REGISTER_METRIC("Spi", maxTurnUs, Gauge, group);

        REGISTER_METRICS_GROUP("SpiDiagnostic", diagnosticGroup);
        REGISTER_METRIC("SpiDiagnostic", crcErr, Counter, diagnosticGroup);
        REGISTER_METRIC("SpiDiagnostic", noSlot, Counter, diagnosticGroup);
        REGISTER_METRIC("SpiDiagnostic", badSlot, Counter, diagnosticGroup);
        REGISTER_METRIC("SpiDiagnostic", missSeq, Counter, diagnosticGroup);
        REGISTER_METRIC("SpiDiagnostic", staleSeq, Counter, diagnosticGroup);
        REGISTER_METRIC("SpiDiagnostic", helloRe, Counter, diagnosticGroup);

        REGISTER_METRICS_GROUP("SpiCore", coreGroup);
        REGISTER_METRIC("SpiCore", resets, Counter, coreGroup);
        REGISTER_METRIC("SpiCore", lrCorr, Counter, coreGroup);
        REGISTER_METRIC("SpiCore", lrCrc, Counter, coreGroup);
        REGISTER_METRIC("SpiCore", lrSeq, Counter, coreGroup);
        REGISTER_METRIC("SpiCore", lrTmo, Counter, coreGroup);
        REGISTER_METRIC("SpiCore", lrHello, Counter, coreGroup);
        REGISTER_METRIC("SpiCore", lrOther, Counter, coreGroup);

        return Metrics{
            .group = group,
            .turns = turns,
            .taskSteps = taskSteps,
            .attentionWakes = attentionWakes,
            .attentionAssert = attentionAssert,
            .attentionRelease = attentionRelease,
            .txBytes = txBytes,
            .rxBytes = rxBytes,
            .slotsRx = slotsRx,
            .framesRx = framesRx,
            .framesTx = framesTx,
            .emptySlots = empty,
            .maxTurnUs = maxTurnUs,
            .diagnosticGroup = diagnosticGroup,
            .crcErrors = crcErr,
            .noSlots = noSlot,
            .badSlots = badSlot,
            .missedSequences = missSeq,
            .staleSequences = staleSeq,
            .helloResyncs = helloRe,
            .coreGroup = coreGroup,
            .resets = resets,
            .lrCorr = lrCorr,
            .lrCrc = lrCrc,
            .lrSeq = lrSeq,
            .lrTmo = lrTmo,
            .lrHello = lrHello,
            .lrOther = lrOther,
        };
    }

    void addTurn() const { METRIC_INCR(group, turns, 1); }
    void addTaskStep() const { METRIC_INCR(group, taskSteps, 1); }
    void addAttentionWake() const { METRIC_INCR(group, attentionWakes, 1); }
    void addAttentionAssert() const { METRIC_INCR(group, attentionAssert, 1); }
    void addAttentionRelease() const {
        METRIC_INCR(group, attentionRelease, 1);
    }
    void addTxBytes(uint32_t bytes) const {
        METRIC_INCR(group, txBytes, bytes);
    }
    void addRxBytes(uint32_t bytes) const {
        METRIC_INCR(group, rxBytes, bytes);
    }
    void addSlotRx() const { METRIC_INCR(group, slotsRx, 1); }
    void addFrameRx() const { METRIC_INCR(group, framesRx, 1); }
    void addFrameTx() const { METRIC_INCR(group, framesTx, 1); }
    void addCrcError() const { METRIC_INCR(diagnosticGroup, crcErrors, 1); }
    void addNoSlot() const { METRIC_INCR(diagnosticGroup, noSlots, 1); }
    void addEmptySlot() const { METRIC_INCR(group, emptySlots, 1); }
    void addBadSlot() const { METRIC_INCR(diagnosticGroup, badSlots, 1); }
    void addMissedSequence() const {
        METRIC_INCR(diagnosticGroup, missedSequences, 1);
    }
    void addStaleSequence() const {
        METRIC_INCR(diagnosticGroup, staleSequences, 1);
    }
    void addHelloResync() const {
        METRIC_INCR(diagnosticGroup, helloResyncs, 1);
    }
    void addReset() const { METRIC_INCR(coreGroup, resets, 1); }

    void addLinkRecovery(LinkRecoveryReason reason) const {
        switch (reason) {
        case LinkRecoveryReason::Corrupted:
            METRIC_INCR(coreGroup, lrCorr, 1);
            return;
        case LinkRecoveryReason::Crc:
            METRIC_INCR(coreGroup, lrCrc, 1);
            return;
        case LinkRecoveryReason::Sequence:
            METRIC_INCR(coreGroup, lrSeq, 1);
            return;
        case LinkRecoveryReason::Timeout:
            METRIC_INCR(coreGroup, lrTmo, 1);
            return;
        case LinkRecoveryReason::HelloResync:
            METRIC_INCR(coreGroup, lrHello, 1);
            return;
        case LinkRecoveryReason::Other:
            METRIC_INCR(coreGroup, lrOther, 1);
            return;
        }
        METRIC_INCR(coreGroup, lrOther, 1);
    }

    void recordTurnDuration(uint32_t durationUs) {
        if (durationUs <= maxTurnUsValue) {
            return;
        }
        maxTurnUsValue = durationUs;
        METRIC_SET(group, maxTurnUs, durationUs);
    }

    GroupHandle group;
    CounterHandle turns;
    CounterHandle taskSteps;
    CounterHandle attentionWakes;
    CounterHandle attentionAssert;
    CounterHandle attentionRelease;
    CounterHandle txBytes;
    CounterHandle rxBytes;
    CounterHandle slotsRx;
    CounterHandle framesRx;
    CounterHandle framesTx;
    CounterHandle emptySlots;
    GaugeHandle maxTurnUs;
    GroupHandle diagnosticGroup;
    CounterHandle crcErrors;
    CounterHandle noSlots;
    CounterHandle badSlots;
    CounterHandle missedSequences;
    CounterHandle staleSequences;
    CounterHandle helloResyncs;
    GroupHandle coreGroup;
    CounterHandle resets;
    CounterHandle lrCorr;
    CounterHandle lrCrc;
    CounterHandle lrSeq;
    CounterHandle lrTmo;
    CounterHandle lrHello;
    CounterHandle lrOther;
    uint32_t maxTurnUsValue = 0;

    static constexpr auto component = MetricsBackend::MetricComponent::Spi;
};

DEFINE_PREWARMED_METRICS_ACCESSORS(Metrics, metrics, prewarmMetrics)

} // namespace Totem::Wire::Spi::detail
