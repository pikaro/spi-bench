#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include <cstdint>

namespace Totem::Buttons::detail {

struct Metrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;
    using MetricUnit = Totem::MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc coreGroupDef = {
        .name = "btnCore",
        .level = Totem::MetricsBackend::MetricLevel::Core,
    };
    static constexpr MetricGroupDesc groupDef = {
        .name = "buttons",
        .level = Totem::MetricsBackend::MetricLevel::Baseline,
    };
    static constexpr MetricGroupDesc diagnosticGroupDef = {
        .name = "btnDiag",
        .level = Totem::MetricsBackend::MetricLevel::Diagnostic,
    };

    static constexpr MetricDesc isrDropsDef = {
        .name = "isrDrop",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc publishFailuresDef = {
        .name = "pubFail",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc publishedDef = {
        .name = "publish",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc isrEventsDef = {
        .name = "isr",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc pollChangesDef = {
        .name = "pollChg",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc duplicatesDef = {
        .name = "dedupe",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc ignoredDef = {
        .name = "ignored",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("ButtonsCore", coreGroup);
        REGISTER_METRIC("ButtonsCore", isrDrops, Counter, coreGroup);
        REGISTER_METRIC("ButtonsCore", publishFailures, Counter, coreGroup);

        REGISTER_METRICS_GROUP("Buttons", group);
        REGISTER_METRIC("Buttons", published, Counter, group);

        REGISTER_METRICS_GROUP("ButtonsDiagnostic", diagnosticGroup);
        REGISTER_METRIC("ButtonsDiagnostic", isrEvents, Counter,
                        diagnosticGroup);
        REGISTER_METRIC("ButtonsDiagnostic", pollChanges, Counter,
                        diagnosticGroup);
        REGISTER_METRIC("ButtonsDiagnostic", duplicates, Counter,
                        diagnosticGroup);
        REGISTER_METRIC("ButtonsDiagnostic", ignored, Counter,
                        diagnosticGroup);

        return Metrics{
            .coreGroup = coreGroup,
            .isrDrops = isrDrops,
            .publishFailures = publishFailures,
            .group = group,
            .published = published,
            .diagnosticGroup = diagnosticGroup,
            .isrEvents = isrEvents,
            .pollChanges = pollChanges,
            .duplicates = duplicates,
            .ignored = ignored,
        };
    }

    void addIsrEvents(uint32_t count) const {
        METRIC_INCR(diagnosticGroup, isrEvents, count);
    }
    void addIsrDrops(uint32_t count) const {
        METRIC_INCR(coreGroup, isrDrops, count);
    }
    void addIsrEvent() const { addIsrEvents(1); }
    void addIsrDrop() const { addIsrDrops(1); }
    void addPollChange() const {
        METRIC_INCR(diagnosticGroup, pollChanges, 1);
    }
    void addDuplicate() const { METRIC_INCR(diagnosticGroup, duplicates, 1); }
    void addIgnored() const { METRIC_INCR(diagnosticGroup, ignored, 1); }
    void addPublishFailure() const {
        METRIC_INCR(coreGroup, publishFailures, 1);
    }
    void addPublished() const { METRIC_INCR(group, published, 1); }

    GroupHandle coreGroup;
    CounterHandle isrDrops;
    CounterHandle publishFailures;
    GroupHandle group;
    CounterHandle published;
    GroupHandle diagnosticGroup;
    CounterHandle isrEvents;
    CounterHandle pollChanges;
    CounterHandle duplicates;
    CounterHandle ignored;

    static constexpr auto component = MetricsBackend::MetricComponent::Buttons;
};

DEFINE_PREWARMED_METRICS_ACCESSORS(Metrics, metrics, prewarmMetrics)

} // namespace Totem::Buttons::detail
