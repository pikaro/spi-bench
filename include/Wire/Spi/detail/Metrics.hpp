#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include "StaticConfig/Metrics.hpp"
#include <cstdint>

namespace Totem::Wire::Spi::detail {

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
        .logLevel = LogLevel::Info,
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
        .name = "attnAssert",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc attentionReleaseDef = {
        .name = "attnRelease",
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
        REGISTER_METRIC("Spi", crcErr, Counter, group);
        REGISTER_METRIC("Spi", resets, Counter, group);
        REGISTER_METRIC("Spi", maxTurnUs, Gauge, group);

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
            .crcErrors = crcErr,
            .resets = resets,
            .maxTurnUs = maxTurnUs,
        };
    }

    void addTurn() const { METRIC_INCR(turns, 1); }
    void addTaskStep() const { METRIC_INCR(taskSteps, 1); }
    void addAttentionWake() const { METRIC_INCR(attentionWakes, 1); }
    void addAttentionAssert() const { METRIC_INCR(attentionAssert, 1); }
    void addAttentionRelease() const { METRIC_INCR(attentionRelease, 1); }
    void addTxBytes(uint32_t bytes) const { METRIC_INCR(txBytes, bytes); }
    void addRxBytes(uint32_t bytes) const { METRIC_INCR(rxBytes, bytes); }
    void addSlotRx() const { METRIC_INCR(slotsRx, 1); }
    void addFrameRx() const { METRIC_INCR(framesRx, 1); }
    void addFrameTx() const { METRIC_INCR(framesTx, 1); }
    void addCrcError() const { METRIC_INCR(crcErrors, 1); }
    void addReset() const { METRIC_INCR(resets, 1); }

    void recordTurnDuration(uint32_t durationUs) {
        if (durationUs <= maxTurnUsValue) {
            return;
        }
        maxTurnUsValue = durationUs;
        METRIC_SET(maxTurnUs, durationUs);
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
    CounterHandle crcErrors;
    CounterHandle resets;
    GaugeHandle maxTurnUs;
    uint32_t maxTurnUsValue = 0;

    static constexpr bool enabled =
        MetricCollection::enabled && MetricCollection::spi;
};

inline Metrics &metrics() {
    static Metrics instance = Metrics::create();
    return instance;
}

} // namespace Totem::Wire::Spi::detail
