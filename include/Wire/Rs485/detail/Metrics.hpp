#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include "StaticConfig/Metrics.hpp"
#include <cstdint>

namespace Totem::Wire::Rs485::detail {

struct Metrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using GaugeHandle = Totem::MetricsBackend::GaugeHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;
    using MetricUnit = Totem::MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc groupDef = {
        .name = "rs485",
        .logLevel = LogLevel::Info,
    };

    static constexpr MetricDesc taskStepsDef = {
        .name = "steps",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc uartDataWakesDef = {
        .name = "uartWake",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc attentionWakesDef = {
        .name = "attnWake",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc pollsDef = {
        .name = "poll",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc emptyPollsDef = {
        .name = "empty",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc receivedFramesDef = {
        .name = "rx",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc txDataFramesDef = {
        .name = "txData",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc txExchangesDef = {
        .name = "txEx",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc txGrantsDef = {
        .name = "txGrant",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc txNopsDef = {
        .name = "txNop",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc syncHeaderReadsDef = {
        .name = "syncHdr",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc syncHeaderTimeoutsDef = {
        .name = "hdrTo",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc syncPayloadReadsDef = {
        .name = "syncPay",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc resetsDef = {
        .name = "resets",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc maxStepUsDef = {
        .name = "maxStep",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("Rs485", group);
        REGISTER_METRIC("Rs485", taskSteps, Counter, group);
        REGISTER_METRIC("Rs485", uartDataWakes, Counter, group);
        REGISTER_METRIC("Rs485", attentionWakes, Counter, group);
        REGISTER_METRIC("Rs485", polls, Counter, group);
        REGISTER_METRIC("Rs485", emptyPolls, Counter, group);
        REGISTER_METRIC("Rs485", receivedFrames, Counter, group);
        REGISTER_METRIC("Rs485", txDataFrames, Counter, group);
        REGISTER_METRIC("Rs485", txExchanges, Counter, group);
        REGISTER_METRIC("Rs485", txGrants, Counter, group);
        REGISTER_METRIC("Rs485", txNops, Counter, group);
        REGISTER_METRIC("Rs485", syncHeaderReads, Counter, group);
        REGISTER_METRIC("Rs485", syncHeaderTimeouts, Counter, group);
        REGISTER_METRIC("Rs485", syncPayloadReads, Counter, group);
        REGISTER_METRIC("Rs485", resets, Counter, group);
        REGISTER_METRIC("Rs485", maxStepUs, Gauge, group);

        return Metrics{
            .group = group,
            .taskSteps = taskSteps,
            .uartDataWakes = uartDataWakes,
            .attentionWakes = attentionWakes,
            .polls = polls,
            .emptyPolls = emptyPolls,
            .receivedFrames = receivedFrames,
            .txDataFrames = txDataFrames,
            .txExchanges = txExchanges,
            .txGrants = txGrants,
            .txNops = txNops,
            .syncHeaderReads = syncHeaderReads,
            .syncHeaderTimeouts = syncHeaderTimeouts,
            .syncPayloadReads = syncPayloadReads,
            .resets = resets,
            .maxStepUs = maxStepUs,
        };
    }

    void addTaskStep() const { METRIC_INCR(taskSteps, 1); }
    void addUartDataWake() const { METRIC_INCR(uartDataWakes, 1); }
    void addAttentionWake() const { METRIC_INCR(attentionWakes, 1); }
    void addPoll() const { METRIC_INCR(polls, 1); }
    void addEmptyPoll() const { METRIC_INCR(emptyPolls, 1); }
    void addReceivedFrame() const { METRIC_INCR(receivedFrames, 1); }
    void addTxDataFrame() const { METRIC_INCR(txDataFrames, 1); }
    void addTxExchange() const { METRIC_INCR(txExchanges, 1); }
    void addTxGrant() const { METRIC_INCR(txGrants, 1); }
    void addTxNop() const { METRIC_INCR(txNops, 1); }
    void addSyncHeaderRead() const { METRIC_INCR(syncHeaderReads, 1); }
    void addSyncHeaderTimeout() const { METRIC_INCR(syncHeaderTimeouts, 1); }
    void addSyncPayloadRead() const { METRIC_INCR(syncPayloadReads, 1); }
    void addReset() const { METRIC_INCR(resets, 1); }

    void recordStepDuration(uint32_t durationUs) {
        if (durationUs <= maxStepUsValue) {
            return;
        }
        maxStepUsValue = durationUs;
        METRIC_SET(maxStepUs, durationUs);
    }

    GroupHandle group;
    CounterHandle taskSteps;
    CounterHandle uartDataWakes;
    CounterHandle attentionWakes;
    CounterHandle polls;
    CounterHandle emptyPolls;
    CounterHandle receivedFrames;
    CounterHandle txDataFrames;
    CounterHandle txExchanges;
    CounterHandle txGrants;
    CounterHandle txNops;
    CounterHandle syncHeaderReads;
    CounterHandle syncHeaderTimeouts;
    CounterHandle syncPayloadReads;
    CounterHandle resets;
    GaugeHandle maxStepUs;
    uint32_t maxStepUsValue = 0;

    static constexpr bool enabled =
        MetricCollection::enabled && MetricCollection::rs485;
};

inline Metrics &metrics() {
    static Metrics instance = Metrics::create();
    return instance;
}

} // namespace Totem::Wire::Rs485::detail
