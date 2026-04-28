#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include "StaticConfig/Metrics.hpp"
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
        .logLevel = LogLevel::Info,
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

    void setDropped(uint32_t count) const { METRIC_SET(droppedRecords, count); }

    void setProcessed(uint32_t count) const {
        METRIC_SET(processedRecords, count);
    }

    GroupHandle group;
    GaugeHandle processedRecords;
    GaugeHandle droppedRecords;

    static constexpr bool enabled =
        MetricCollection::enabled && MetricCollection::logging;
};

} // namespace Totem::LoggingBackend::detail
