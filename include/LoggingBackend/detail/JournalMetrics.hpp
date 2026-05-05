#pragma once

#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include <cstdint>

namespace Totem::LoggingBackend::detail {

struct JournalMetrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using GaugeHandle = Totem::MetricsBackend::GaugeHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;
    using MetricUnit = Totem::MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc groupDef = {
        .name = "logJrn",
        .level = Totem::MetricsBackend::MetricLevel::Core,
    };

    static constexpr MetricDesc caughtDef = {
        .name = "caught",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc writtenDef = {
        .name = "written",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc slotDropsDef = {
        .name = "slotDrop",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc siteDropsDef = {
        .name = "siteDrop",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc queueDropsDef = {
        .name = "qDrop",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc writeFailuresDef = {
        .name = "wrFail",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc flashFullDef = {
        .name = "full",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc disabledDef = {
        .name = "off",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc activeSlotsDef = {
        .name = "active",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
    };

    static JournalMetrics create() {
        auto groupResult = MetricsService::registrar().addGroup(
            groupDef, metrics_enabled(component, groupDef));
        if (!groupResult) {
            return disabled();
        }

        auto caughtResult = MetricsService::registrar().addGauge(
            groupResult->key(), caughtDef, metrics_enabled(component, groupDef));
        auto writtenResult = MetricsService::registrar().addGauge(
            groupResult->key(), writtenDef,
            metrics_enabled(component, groupDef));
        auto slotDropsResult = MetricsService::registrar().addGauge(
            groupResult->key(), slotDropsDef,
            metrics_enabled(component, groupDef));
        auto siteDropsResult = MetricsService::registrar().addGauge(
            groupResult->key(), siteDropsDef,
            metrics_enabled(component, groupDef));
        auto queueDropsResult = MetricsService::registrar().addGauge(
            groupResult->key(), queueDropsDef,
            metrics_enabled(component, groupDef));
        auto writeFailuresResult = MetricsService::registrar().addGauge(
            groupResult->key(), writeFailuresDef,
            metrics_enabled(component, groupDef));
        auto flashFullResult = MetricsService::registrar().addGauge(
            groupResult->key(), flashFullDef,
            metrics_enabled(component, groupDef));
        auto disabledResult = MetricsService::registrar().addGauge(
            groupResult->key(), disabledDef,
            metrics_enabled(component, groupDef));
        auto activeSlotsResult = MetricsService::registrar().addGauge(
            groupResult->key(), activeSlotsDef,
            metrics_enabled(component, groupDef));

        if (!caughtResult || !writtenResult || !slotDropsResult ||
            !siteDropsResult || !queueDropsResult || !writeFailuresResult ||
            !flashFullResult || !disabledResult || !activeSlotsResult) {
            return disabled();
        }

        return JournalMetrics{
            .enabled = true,
            .group = *groupResult,
            .caught = *caughtResult,
            .written = *writtenResult,
            .slotDrops = *slotDropsResult,
            .siteDrops = *siteDropsResult,
            .queueDrops = *queueDropsResult,
            .writeFailures = *writeFailuresResult,
            .flashFull = *flashFullResult,
            .disabledGauge = *disabledResult,
            .activeSlots = *activeSlotsResult,
        };
    }

    static JournalMetrics disabled() {
        return JournalMetrics{
            .enabled = false,
            .group = GroupHandle::null(),
            .caught = GaugeHandle::null(),
            .written = GaugeHandle::null(),
            .slotDrops = GaugeHandle::null(),
            .siteDrops = GaugeHandle::null(),
            .queueDrops = GaugeHandle::null(),
            .writeFailures = GaugeHandle::null(),
            .flashFull = GaugeHandle::null(),
            .disabledGauge = GaugeHandle::null(),
            .activeSlots = GaugeHandle::null(),
        };
    }

    void setCaught(uint32_t count) const { _set(caught, count); }
    void setWritten(uint32_t count) const { _set(written, count); }
    void setSlotDrops(uint32_t count) const {
        _set(slotDrops, count);
    }
    void setSiteDrops(uint32_t count) const {
        _set(siteDrops, count);
    }
    void setQueueDrops(uint32_t count) const {
        _set(queueDrops, count);
    }
    void setWriteFailures(uint32_t count) const {
        _set(writeFailures, count);
    }
    void setFlashFull(uint32_t count) const {
        _set(flashFull, count);
    }
    void setDisabled(bool value) const {
        _set(disabledGauge, value ? 1U : 0U);
    }
    void setActiveSlots(uint32_t count) const {
        _set(activeSlots, count);
    }

    bool enabled;
    GroupHandle group;
    GaugeHandle caught;
    GaugeHandle written;
    GaugeHandle slotDrops;
    GaugeHandle siteDrops;
    GaugeHandle queueDrops;
    GaugeHandle writeFailures;
    GaugeHandle flashFull;
    GaugeHandle disabledGauge;
    GaugeHandle activeSlots;

    static constexpr auto component = MetricsBackend::MetricComponent::Logging;

  private:
    void _set(GaugeHandle handle, uint32_t value) const {
        if (!enabled) {
            return;
        }
        (void)::MetricsService::recorder().set(handle, value);
    }
};

} // namespace Totem::LoggingBackend::detail
