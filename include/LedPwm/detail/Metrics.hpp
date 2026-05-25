#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include <cstdint>

namespace Totem::LedPwm::detail {

struct Metrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;
    using MetricUnit = Totem::MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc coreGroupDef = {
        .name = "ledCore",
        .level = Totem::MetricsBackend::MetricLevel::Core,
    };
    static constexpr MetricGroupDesc groupDef = {
        .name = "ledPwm",
        .level = Totem::MetricsBackend::MetricLevel::Baseline,
    };
    static constexpr MetricGroupDesc profileGroupDef = {
        .name = "ledProf",
        .level = Totem::MetricsBackend::MetricLevel::Profiling,
    };

    static constexpr MetricDesc queueFailuresDef = {
        .name = "qFail",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc handleFailuresDef = {
        .name = "hFail",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc queuedDef = {
        .name = "queued",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc handledDef = {
        .name = "handled",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc taskStepsDef = {
        .name = "steps",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("LedPwmCore", coreGroup);
        REGISTER_METRIC("LedPwmCore", queueFailures, Counter, coreGroup);
        REGISTER_METRIC("LedPwmCore", handleFailures, Counter, coreGroup);

        REGISTER_METRICS_GROUP("LedPwm", group);
        REGISTER_METRIC("LedPwm", queued, Counter, group);
        REGISTER_METRIC("LedPwm", handled, Counter, group);

        REGISTER_METRICS_GROUP("LedPwmProfiling", profileGroup);
        REGISTER_METRIC("LedPwmProfiling", taskSteps, Counter, profileGroup);

        return Metrics{
            .coreGroup = coreGroup,
            .queueFailures = queueFailures,
            .handleFailures = handleFailures,
            .group = group,
            .queued = queued,
            .handled = handled,
            .profileGroup = profileGroup,
            .taskSteps = taskSteps,
        };
    }

    void addQueueFailure() const {
        METRIC_INCR(coreGroup, queueFailures, 1);
    }
    void addHandleFailure() const {
        METRIC_INCR(coreGroup, handleFailures, 1);
    }
    void addQueued() const { METRIC_INCR(group, queued, 1); }
    void addHandled() const { METRIC_INCR(group, handled, 1); }
    void addTaskStep() const { METRIC_INCR(profileGroup, taskSteps, 1); }

    GroupHandle coreGroup;
    CounterHandle queueFailures;
    CounterHandle handleFailures;
    GroupHandle group;
    CounterHandle queued;
    CounterHandle handled;
    GroupHandle profileGroup;
    CounterHandle taskSteps;

    static constexpr auto component = MetricsBackend::MetricComponent::LedPwm;
};

DEFINE_PREWARMED_METRICS_ACCESSORS(Metrics, metrics, prewarmMetrics)

} // namespace Totem::LedPwm::detail
