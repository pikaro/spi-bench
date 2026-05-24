#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include <cstdint>

namespace Totem::Bluetooth::detail {

struct Metrics {
    using GroupHandle = MetricsBackend::GroupHandle;
    using CounterHandle = MetricsBackend::CounterHandle;
    using GaugeHandle = MetricsBackend::GaugeHandle;
    using MetricGroupDesc = MetricsBackend::MetricGroupDesc;
    using MetricDesc = MetricsBackend::MetricDesc;
    using MetricType = MetricsBackend::MetricType;
    using MetricUnit = MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc groupDef = {
        .name = "ble",
        .level = MetricsBackend::MetricLevel::Baseline,
    };

    static constexpr MetricDesc scanDef = {
        .name = "scan",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc connDef = {
        .name = "conn",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc discDef = {
        .name = "disc",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc subDef = {
        .name = "sub",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc notifDef = {
        .name = "notif",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc dropDef = {
        .name = "drop",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc failDef = {
        .name = "fail",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc stateDef = {
        .name = "state",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("Bluetooth", group);
        REGISTER_METRIC("Bluetooth", scan, Counter, group);
        REGISTER_METRIC("Bluetooth", conn, Counter, group);
        REGISTER_METRIC("Bluetooth", disc, Counter, group);
        REGISTER_METRIC("Bluetooth", sub, Counter, group);
        REGISTER_METRIC("Bluetooth", notif, Counter, group);
        REGISTER_METRIC("Bluetooth", drop, Counter, group);
        REGISTER_METRIC("Bluetooth", fail, Counter, group);
        REGISTER_METRIC("Bluetooth", state, Gauge, group);

        return Metrics{
            .group = group,
            .scan = scan,
            .conn = conn,
            .disc = disc,
            .sub = sub,
            .notif = notif,
            .drop = drop,
            .fail = fail,
            .state = state,
        };
    }

    void addScan() const { METRIC_INCR(group, scan, 1); }
    void addConn() const { METRIC_INCR(group, conn, 1); }
    void addDisc() const { METRIC_INCR(group, disc, 1); }
    void addSub() const { METRIC_INCR(group, sub, 1); }
    void addNotif() const { METRIC_INCR(group, notif, 1); }
    void addDrop() const { METRIC_INCR(group, drop, 1); }
    void addFail() const { METRIC_INCR(group, fail, 1); }
    void setState(uint32_t value) const { METRIC_SET(group, state, value); }

    GroupHandle group;
    CounterHandle scan;
    CounterHandle conn;
    CounterHandle disc;
    CounterHandle sub;
    CounterHandle notif;
    CounterHandle drop;
    CounterHandle fail;
    GaugeHandle state;

    static constexpr auto component = MetricsBackend::MetricComponent::Bluetooth;
};

inline Metrics &metrics() {
    static Metrics instance = Metrics::create();
    return instance;
}

} // namespace Totem::Bluetooth::detail
