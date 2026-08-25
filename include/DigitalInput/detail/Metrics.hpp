// IWYU pragma: private

#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include <cstdint>

namespace Totem::DigitalInput::detail {

struct Metrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;

    static constexpr MetricGroupDesc groupDef = {
        .name = "inputs",
        .level = Totem::MetricsBackend::MetricLevel::Baseline,
    };
    static constexpr MetricGroupDesc diagnosticGroupDef = {
        .name = "inpDiag",
        .level = Totem::MetricsBackend::MetricLevel::Diagnostic,
    };

    static constexpr MetricDesc eventsDef = {
        .name = "event",
        .type = MetricType::Counter,
    };
    static constexpr MetricDesc isrEventsDef = {
        .name = "isr",
        .type = MetricType::Counter,
    };
    static constexpr MetricDesc pollChangesDef = {
        .name = "pollChg",
        .type = MetricType::Counter,
    };
    static constexpr MetricDesc duplicatesDef = {
        .name = "dedupe",
        .type = MetricType::Counter,
    };
    static constexpr MetricDesc debouncedEventsDef = {
        .name = "dbEvt",
        .type = MetricType::Counter,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("DigitalInput", group);
        REGISTER_METRIC("DigitalInput", events, Counter, group);

        REGISTER_METRICS_GROUP("DigitalInputDiagnostic", diagnosticGroup);
        REGISTER_METRIC("DigitalInputDiagnostic", isrEvents, Counter,
                        diagnosticGroup);
        REGISTER_METRIC("DigitalInputDiagnostic", pollChanges, Counter,
                        diagnosticGroup);
        REGISTER_METRIC("DigitalInputDiagnostic", duplicates, Counter,
                        diagnosticGroup);
        REGISTER_METRIC("DigitalInputDiagnostic", debouncedEvents, Counter,
                        diagnosticGroup);

        return Metrics{
            .group = group,
            .events = events,
            .diagnosticGroup = diagnosticGroup,
            .isrEvents = isrEvents,
            .pollChanges = pollChanges,
            .duplicates = duplicates,
            .debouncedEvents = debouncedEvents,
        };
    }

    void addEvents(uint32_t count) const { METRIC_INCR(group, events, count); }
    void addIsrEvents(uint32_t count) const {
        METRIC_INCR(diagnosticGroup, isrEvents, count);
    }
    void addPollChanges(uint32_t count) const {
        METRIC_INCR(diagnosticGroup, pollChanges, count);
    }
    void addDuplicates(uint32_t count) const {
        METRIC_INCR(diagnosticGroup, duplicates, count);
    }
    void addDebouncedEvents(uint32_t count) const {
        METRIC_INCR(diagnosticGroup, debouncedEvents, count);
    }

    GroupHandle group;
    CounterHandle events;
    GroupHandle diagnosticGroup;
    CounterHandle isrEvents;
    CounterHandle pollChanges;
    CounterHandle duplicates;
    CounterHandle debouncedEvents;

    static constexpr auto component = MetricsBackend::MetricComponent::Input;
};

DEFINE_PREWARMED_METRICS_ACCESSORS(Metrics, metrics, prewarmMetrics)

} // namespace Totem::DigitalInput::detail
