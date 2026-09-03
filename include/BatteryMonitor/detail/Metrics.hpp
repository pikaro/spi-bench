// IWYU pragma: private

#pragma once

#include "BatteryMonitor/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep

namespace Totem::BatteryMonitor::detail {

struct Metrics {
    using CounterHandle = MetricsBackend::CounterHandle;
    using GaugeHandle = MetricsBackend::GaugeHandle;
    using GroupHandle = MetricsBackend::GroupHandle;
    using MetricDesc = MetricsBackend::MetricDesc;
    using MetricGroupDesc = MetricsBackend::MetricGroupDesc;
    using MetricType = MetricsBackend::MetricType;
    using MetricUnit = MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc baseDef{
        .name = "battery",
        .level = MetricsBackend::MetricLevel::Baseline,
    };
    static constexpr MetricDesc socPptDef{.name = "socPpt",
                                          .type = MetricType::Gauge};
    static constexpr MetricDesc remMahDef{.name = "remMah",
                                          .type = MetricType::Gauge};
    static constexpr MetricDesc remMwhDef{.name = "remMwh",
                                          .type = MetricType::Gauge};
    static constexpr MetricDesc usedMahDef{.name = "usedMah",
                                           .type = MetricType::Gauge};
    static constexpr MetricDesc usedMwhDef{.name = "usedMwh",
                                           .type = MetricType::Gauge};
    static constexpr MetricDesc avgMwDef{.name = "avgMw",
                                         .type = MetricType::Gauge};
    static constexpr MetricDesc tteMinDef{
        .name = "tteMin",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Minutes,
    };
    static constexpr MetricDesc tteValidDef{.name = "tteValid",
                                            .type = MetricType::Gauge};
    static constexpr MetricDesc freshDef{.name = "fresh",
                                         .type = MetricType::Gauge};
    static constexpr MetricDesc storageDef{.name = "storage",
                                           .type = MetricType::Gauge};
    static constexpr MetricDesc confDef{.name = "conf",
                                        .type = MetricType::Gauge};
    static constexpr MetricDesc calStatDef{.name = "calStat",
                                           .type = MetricType::Gauge};
    static constexpr MetricDesc calIntsDef{.name = "calInts",
                                           .type = MetricType::Counter};
    static constexpr MetricDesc gapMaxDef{
        .name = "gapMax",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Milliseconds,
    };
    static constexpr MetricDesc failDef{.name = "fail",
                                        .type = MetricType::Counter};
    static constexpr MetricDesc fsFailDef{.name = "fsFail",
                                          .type = MetricType::Counter};

    static Metrics create(const MetricGroupDesc &groupDef) {
        REGISTER_METRICS_GROUP("BatteryMonitor", group);
        REGISTER_METRIC("BatteryMonitor", socPpt, Gauge, group);
        REGISTER_METRIC("BatteryMonitor", remMah, Gauge, group);
        REGISTER_METRIC("BatteryMonitor", remMwh, Gauge, group);
        REGISTER_METRIC("BatteryMonitor", usedMah, Gauge, group);
        REGISTER_METRIC("BatteryMonitor", usedMwh, Gauge, group);
        REGISTER_METRIC("BatteryMonitor", avgMw, Gauge, group);
        REGISTER_METRIC("BatteryMonitor", tteMin, Gauge, group);
        REGISTER_METRIC("BatteryMonitor", tteValid, Gauge, group);
        REGISTER_METRIC("BatteryMonitor", fresh, Gauge, group);
        REGISTER_METRIC("BatteryMonitor", storage, Gauge, group);
        REGISTER_METRIC("BatteryMonitor", conf, Gauge, group);
        REGISTER_METRIC("BatteryMonitor", calStat, Gauge, group);
        REGISTER_METRIC("BatteryMonitor", calInts, Counter, group);
        REGISTER_METRIC("BatteryMonitor", gapMax, Gauge, group);
        REGISTER_METRIC("BatteryMonitor", fail, Counter, group);
        REGISTER_METRIC("BatteryMonitor", fsFail, Counter, group);
        return {
            .group = group,
            .socPpt = socPpt,
            .remMah = remMah,
            .remMwh = remMwh,
            .usedMah = usedMah,
            .usedMwh = usedMwh,
            .avgMw = avgMw,
            .tteMin = tteMin,
            .tteValid = tteValid,
            .fresh = fresh,
            .storage = storage,
            .conf = conf,
            .calStat = calStat,
            .calInts = calInts,
            .gapMax = gapMax,
            .fail = fail,
            .fsFail = fsFail,
        };
    }

    void initialize() const {
        const BatteryStatus empty{};
        record(empty);
    }

    void record(const BatteryStatus &status) const {
        METRIC_SET(base, socPpt, status.stateOfChargePartsPerThousand);
        METRIC_SET(base, remMah, status.remainingMilliampHours);
        METRIC_SET(base, remMwh, status.remainingMilliwattHours);
        METRIC_SET(base, usedMah, status.dischargedMilliampHours);
        METRIC_SET(base, usedMwh, status.dischargedMilliwattHours);
        METRIC_SET(base, avgMw, status.averagePowerMilliwatts);
        METRIC_SET(base, tteMin, status.timeToEmptyMinutes.value_or(0));
        METRIC_SET(base, tteValid, status.timeToEmptyMinutes.has_value());
        METRIC_SET(base, fresh,
                   static_cast<uint32_t>(status.measurementFreshness));
        METRIC_SET(base, storage, static_cast<uint32_t>(status.storageHealth));
        METRIC_SET(base, conf, static_cast<uint32_t>(status.confidence));
        METRIC_SET(base, calStat,
                   static_cast<uint32_t>(status.calibrationState));
        METRIC_SET(base, gapMax, status.maximumSampleGapMs);
    }

    void addCalibrationInterval() const { METRIC_INCR(base, calInts, 1); }
    void addFailure() const { METRIC_INCR(base, fail, 1); }
    void addFileSystemFailure() const { METRIC_INCR(base, fsFail, 1); }

    GroupHandle group;
    GaugeHandle socPpt;
    GaugeHandle remMah;
    GaugeHandle remMwh;
    GaugeHandle usedMah;
    GaugeHandle usedMwh;
    GaugeHandle avgMw;
    GaugeHandle tteMin;
    GaugeHandle tteValid;
    GaugeHandle fresh;
    GaugeHandle storage;
    GaugeHandle conf;
    GaugeHandle calStat;
    CounterHandle calInts;
    GaugeHandle gapMax;
    CounterHandle fail;
    CounterHandle fsFail;

    static constexpr auto component = MetricsBackend::MetricComponent::Battery;
};

} // namespace Totem::BatteryMonitor::detail
