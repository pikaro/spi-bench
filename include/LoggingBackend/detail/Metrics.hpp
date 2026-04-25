#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp"
#include "Types/Error.hpp"
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
        .unit = MetricUnit::Items,
    };

    static constexpr MetricDesc droppedRecordsDef = {
        .name = "droppedR",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Items,
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

    ReturnCode setDropped(uint32_t count) const {
        return ::MetricsService::recorder().set(droppedRecords, count);
    }

    ReturnCode setProcessed(uint32_t count) const {
        return ::MetricsService::recorder().set(processedRecords, count);
    }

    GroupHandle group;
    GaugeHandle processedRecords;
    GaugeHandle droppedRecords;
};

} // namespace Totem::LoggingBackend::detail
