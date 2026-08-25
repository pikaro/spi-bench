// IWYU pragma: private

#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include <cstdint>

namespace Totem::PubSubEventProducer::detail {

struct Metrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;

    static constexpr MetricGroupDesc coreGroupDef = {
        .name = "evtCore",
        .level = Totem::MetricsBackend::MetricLevel::Core,
    };
    static constexpr MetricGroupDesc groupDef = {
        .name = "events",
        .level = Totem::MetricsBackend::MetricLevel::Baseline,
    };
    static constexpr MetricGroupDesc diagnosticGroupDef = {
        .name = "evtDiag",
        .level = Totem::MetricsBackend::MetricLevel::Diagnostic,
    };

    static constexpr MetricDesc queueDropsDef = {
        .name = "qDrop",
        .type = MetricType::Counter,
    };
    static constexpr MetricDesc publishFailuresDef = {
        .name = "pubFail",
        .type = MetricType::Counter,
    };
    static constexpr MetricDesc enqueuedDef = {
        .name = "enqueue",
        .type = MetricType::Counter,
    };
    static constexpr MetricDesc publishedDef = {
        .name = "publish",
        .type = MetricType::Counter,
    };
    static constexpr MetricDesc isrRequestsDef = {
        .name = "isr",
        .type = MetricType::Counter,
    };
    static constexpr MetricDesc taskRequestsDef = {
        .name = "task",
        .type = MetricType::Counter,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("PubSubEventProducerCore", coreGroup);
        REGISTER_METRIC("PubSubEventProducerCore", queueDrops, Counter,
                        coreGroup);
        REGISTER_METRIC("PubSubEventProducerCore", publishFailures, Counter,
                        coreGroup);

        REGISTER_METRICS_GROUP("PubSubEventProducer", group);
        REGISTER_METRIC("PubSubEventProducer", enqueued, Counter, group);
        REGISTER_METRIC("PubSubEventProducer", published, Counter, group);

        REGISTER_METRICS_GROUP("PubSubEventProducerDiagnostic",
                               diagnosticGroup);
        REGISTER_METRIC("PubSubEventProducerDiagnostic", isrRequests, Counter,
                        diagnosticGroup);
        REGISTER_METRIC("PubSubEventProducerDiagnostic", taskRequests, Counter,
                        diagnosticGroup);

        return Metrics{
            .coreGroup = coreGroup,
            .queueDrops = queueDrops,
            .publishFailures = publishFailures,
            .group = group,
            .enqueued = enqueued,
            .published = published,
            .diagnosticGroup = diagnosticGroup,
            .isrRequests = isrRequests,
            .taskRequests = taskRequests,
        };
    }

    void addQueueDrops(uint32_t count) const {
        METRIC_INCR(coreGroup, queueDrops, count);
    }
    void addPublishFailure() const {
        METRIC_INCR(coreGroup, publishFailures, 1);
    }
    void addEnqueued(uint32_t count) const {
        METRIC_INCR(group, enqueued, count);
    }
    void addPublished() const { METRIC_INCR(group, published, 1); }
    void addIsrRequests(uint32_t count) const {
        METRIC_INCR(diagnosticGroup, isrRequests, count);
    }
    void addTaskRequests(uint32_t count) const {
        METRIC_INCR(diagnosticGroup, taskRequests, count);
    }

    GroupHandle coreGroup;
    CounterHandle queueDrops;
    CounterHandle publishFailures;
    GroupHandle group;
    CounterHandle enqueued;
    CounterHandle published;
    GroupHandle diagnosticGroup;
    CounterHandle isrRequests;
    CounterHandle taskRequests;

    static constexpr auto component = MetricsBackend::MetricComponent::PubSub;
};

DEFINE_PREWARMED_METRICS_ACCESSORS(Metrics, metrics, prewarmMetrics)

} // namespace Totem::PubSubEventProducer::detail
