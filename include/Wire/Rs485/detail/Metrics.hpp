#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
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

    static constexpr MetricGroupDesc coreGroupDef = {
        .name = "rs4Core",
        .level = Totem::MetricsBackend::MetricLevel::Core,
    };
    static constexpr MetricGroupDesc groupDef = {
        .name = "rs485",
        .level = Totem::MetricsBackend::MetricLevel::Diagnostic,
    };
    static constexpr MetricGroupDesc profileGroupDef = {
        .name = "rs4Prof",
        .level = Totem::MetricsBackend::MetricLevel::Profiling,
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
    static constexpr MetricDesc uartOverflowWakesDef = {
        .name = "uartOvf",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc uartErrorWakesDef = {
        .name = "uartErr",
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
        REGISTER_METRICS_GROUP("Rs485Core", coreGroup);
        REGISTER_METRIC("Rs485Core", uartOverflowWakes, Counter, coreGroup);
        REGISTER_METRIC("Rs485Core", uartErrorWakes, Counter, coreGroup);
        REGISTER_METRIC("Rs485Core", syncHeaderTimeouts, Counter, coreGroup);
        REGISTER_METRIC("Rs485Core", resets, Counter, coreGroup);

        REGISTER_METRICS_GROUP("Rs485", group);
        REGISTER_METRIC("Rs485", uartDataWakes, Counter, group);
        REGISTER_METRIC("Rs485", attentionWakes, Counter, group);
        REGISTER_METRIC("Rs485", receivedFrames, Counter, group);
        REGISTER_METRIC("Rs485", txDataFrames, Counter, group);
        REGISTER_METRIC("Rs485", txExchanges, Counter, group);
        REGISTER_METRIC("Rs485", txGrants, Counter, group);
        REGISTER_METRIC("Rs485", txNops, Counter, group);
        REGISTER_METRIC("Rs485", syncHeaderReads, Counter, group);
        REGISTER_METRIC("Rs485", syncPayloadReads, Counter, group);

        REGISTER_METRICS_GROUP("Rs485Profiling", profileGroup);
        REGISTER_METRIC("Rs485Profiling", taskSteps, Counter, profileGroup);
        REGISTER_METRIC("Rs485Profiling", polls, Counter, profileGroup);
        REGISTER_METRIC("Rs485Profiling", emptyPolls, Counter, profileGroup);
        REGISTER_METRIC("Rs485Profiling", maxStepUs, Gauge, profileGroup);

        return Metrics{
            .coreGroup = coreGroup,
            .uartOverflowWakes = uartOverflowWakes,
            .uartErrorWakes = uartErrorWakes,
            .syncHeaderTimeouts = syncHeaderTimeouts,
            .resets = resets,
            .group = group,
            .uartDataWakes = uartDataWakes,
            .attentionWakes = attentionWakes,
            .receivedFrames = receivedFrames,
            .txDataFrames = txDataFrames,
            .txExchanges = txExchanges,
            .txGrants = txGrants,
            .txNops = txNops,
            .syncHeaderReads = syncHeaderReads,
            .syncPayloadReads = syncPayloadReads,
            .profileGroup = profileGroup,
            .taskSteps = taskSteps,
            .polls = polls,
            .emptyPolls = emptyPolls,
            .maxStepUs = maxStepUs,
        };
    }

    void addTaskStep() const { METRIC_INCR(profileGroup, taskSteps, 1); }
    void addUartDataWake() const { METRIC_INCR(group, uartDataWakes, 1); }
    void addAttentionWake() const { METRIC_INCR(group, attentionWakes, 1); }
    void addUartOverflowWake() const {
        METRIC_INCR(coreGroup, uartOverflowWakes, 1);
    }
    void addUartErrorWake() const {
        METRIC_INCR(coreGroup, uartErrorWakes, 1);
    }
    void addPoll() const { METRIC_INCR(profileGroup, polls, 1); }
    void addEmptyPoll() const { METRIC_INCR(profileGroup, emptyPolls, 1); }
    void addReceivedFrame() const { METRIC_INCR(group, receivedFrames, 1); }
    void addTxDataFrame() const { METRIC_INCR(group, txDataFrames, 1); }
    void addTxExchange() const { METRIC_INCR(group, txExchanges, 1); }
    void addTxGrant() const { METRIC_INCR(group, txGrants, 1); }
    void addTxNop() const { METRIC_INCR(group, txNops, 1); }
    void addSyncHeaderRead() const { METRIC_INCR(group, syncHeaderReads, 1); }
    void addSyncHeaderTimeout() const {
        METRIC_INCR(coreGroup, syncHeaderTimeouts, 1);
    }
    void addSyncPayloadRead() const { METRIC_INCR(group, syncPayloadReads, 1); }
    void addReset() const { METRIC_INCR(coreGroup, resets, 1); }

    void recordStepDuration(uint32_t durationUs) {
        if (durationUs <= maxStepUsValue) {
            return;
        }
        maxStepUsValue = durationUs;
        METRIC_SET(profileGroup, maxStepUs, durationUs);
    }

    GroupHandle coreGroup;
    CounterHandle uartOverflowWakes;
    CounterHandle uartErrorWakes;
    CounterHandle syncHeaderTimeouts;
    CounterHandle resets;
    GroupHandle group;
    CounterHandle uartDataWakes;
    CounterHandle attentionWakes;
    CounterHandle receivedFrames;
    CounterHandle txDataFrames;
    CounterHandle txExchanges;
    CounterHandle txGrants;
    CounterHandle txNops;
    CounterHandle syncHeaderReads;
    CounterHandle syncPayloadReads;
    GroupHandle profileGroup;
    CounterHandle taskSteps;
    CounterHandle polls;
    CounterHandle emptyPolls;
    GaugeHandle maxStepUs;
    uint32_t maxStepUsValue = 0;

    static constexpr auto component = MetricsBackend::MetricComponent::Rs485;
    static constexpr bool profilingEnabled =
        metrics_enabled(component, profileGroupDef);
};

inline Metrics &metrics() {
    static Metrics instance = Metrics::create();
    return instance;
}

} // namespace Totem::Wire::Rs485::detail
