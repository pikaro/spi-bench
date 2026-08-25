// IWYU pragma: private

#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include <cstdint>

namespace Totem::Button::detail {

struct Metrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;

    static constexpr MetricGroupDesc groupDef = {
        .name = "buttons",
        .level = Totem::MetricsBackend::MetricLevel::Baseline,
    };
    static constexpr MetricGroupDesc diagnosticGroupDef = {
        .name = "btnDiag",
        .level = Totem::MetricsBackend::MetricLevel::Diagnostic,
    };

    static constexpr MetricDesc eventsDef = {
        .name = "event",
        .type = MetricType::Counter,
    };
    static constexpr MetricDesc ignoredDef = {
        .name = "ignored",
        .type = MetricType::Counter,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("Button", group);
        REGISTER_METRIC("Button", events, Counter, group);

        REGISTER_METRICS_GROUP("ButtonDiagnostic", diagnosticGroup);
        REGISTER_METRIC("ButtonDiagnostic", ignored, Counter, diagnosticGroup);

        return Metrics{
            .group = group,
            .events = events,
            .diagnosticGroup = diagnosticGroup,
            .ignored = ignored,
        };
    }

    void addEvents(uint32_t count) const { METRIC_INCR(group, events, count); }
    void addIgnored(uint32_t count) const {
        METRIC_INCR(diagnosticGroup, ignored, count);
    }

    GroupHandle group;
    CounterHandle events;
    GroupHandle diagnosticGroup;
    CounterHandle ignored;

    static constexpr auto component = MetricsBackend::MetricComponent::Input;
};

DEFINE_PREWARMED_METRICS_ACCESSORS(Metrics, metrics, prewarmMetrics)

} // namespace Totem::Button::detail
