#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include <cstdint>

namespace Totem::LoggingBackend::detail {

struct Metrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using GaugeHandle = Totem::MetricsBackend::GaugeHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;
    using MetricUnit = Totem::MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc groupDef = {
        .name = "logging",
        .level = Totem::MetricsBackend::MetricLevel::Baseline,
    };

    static constexpr MetricDesc processedRecordsDef = {
        .name = "processd",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
    };

    static constexpr MetricDesc droppedRecordsDef = {
        .name = "droppedR",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("Logging", group);
        REGISTER_METRIC("Logging", processedRecords, Gauge, group);
        REGISTER_METRIC("Logging", droppedRecords, Gauge, group);

        return Metrics{
            .group = group,
            .processedRecords = processedRecords,
            .droppedRecords = droppedRecords,
        };
    }

    void setDropped(uint32_t count) const {
        METRIC_SET(group, droppedRecords, count);
    }

    void setProcessed(uint32_t count) const {
        METRIC_SET(group, processedRecords, count);
    }

    GroupHandle group;
    GaugeHandle processedRecords;
    GaugeHandle droppedRecords;

    static constexpr auto component = MetricsBackend::MetricComponent::Logging;
};

} // namespace Totem::LoggingBackend::detail
