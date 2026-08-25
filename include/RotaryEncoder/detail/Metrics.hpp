// IWYU pragma: private

#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include <cstdint>

namespace Totem::RotaryEncoder::detail {

struct Metrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;

    static constexpr MetricGroupDesc groupDef = {
        .name = "encoder",
        .level = Totem::MetricsBackend::MetricLevel::Baseline,
    };
    static constexpr MetricGroupDesc diagnosticGroupDef = {
        .name = "encDiag",
        .level = Totem::MetricsBackend::MetricLevel::Diagnostic,
    };
    static constexpr MetricDesc clockwiseDef = {
        .name = "clockwise",
        .type = MetricType::Counter,
    };
    static constexpr MetricDesc counterclockwiseDef = {
        .name = "counterclockwise",
        .type = MetricType::Counter,
    };
    static constexpr MetricDesc invalidDef = {
        .name = "invalid",
        .type = MetricType::Counter,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("RotaryEncoder", group);
        REGISTER_METRIC("RotaryEncoder", clockwise, Counter, group);
        REGISTER_METRIC("RotaryEncoder", counterclockwise, Counter, group);

        REGISTER_METRICS_GROUP("RotaryEncoderDiagnostic", diagnosticGroup);
        REGISTER_METRIC("RotaryEncoderDiagnostic", invalid, Counter,
                        diagnosticGroup);

        return Metrics{
            .group = group,
            .clockwise = clockwise,
            .counterclockwise = counterclockwise,
            .diagnosticGroup = diagnosticGroup,
            .invalid = invalid,
        };
    }

    void addClockwise(uint32_t count) const {
        METRIC_INCR(group, clockwise, count);
    }
    void addCounterclockwise(uint32_t count) const {
        METRIC_INCR(group, counterclockwise, count);
    }
    void addInvalid(uint32_t count) const {
        METRIC_INCR(diagnosticGroup, invalid, count);
    }

    GroupHandle group;
    CounterHandle clockwise;
    CounterHandle counterclockwise;
    GroupHandle diagnosticGroup;
    CounterHandle invalid;

    static constexpr auto component = MetricsBackend::MetricComponent::Input;
};

DEFINE_PREWARMED_METRICS_ACCESSORS(Metrics, metrics, prewarmMetrics)

} // namespace Totem::RotaryEncoder::detail
