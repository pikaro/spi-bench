#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep

namespace Totem::Wheel::detail {

struct Metrics {
    using GroupHandle = MetricsBackend::GroupHandle;
    using CounterHandle = MetricsBackend::CounterHandle;
    using MetricGroupDesc = MetricsBackend::MetricGroupDesc;
    using MetricDesc = MetricsBackend::MetricDesc;
    using MetricType = MetricsBackend::MetricType;
    using MetricUnit = MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc groupDef = {
        .name = "wheel",
        .level = MetricsBackend::MetricLevel::Baseline,
    };

    static constexpr MetricDesc notifDef = {
        .name = "notif",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc publishDef = {
        .name = "publish",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc badDef = {
        .name = "bad",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc failDef = {
        .name = "fail",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("Wheel", group);
        REGISTER_METRIC("Wheel", notif, Counter, group);
        REGISTER_METRIC("Wheel", publish, Counter, group);
        REGISTER_METRIC("Wheel", bad, Counter, group);
        REGISTER_METRIC("Wheel", fail, Counter, group);

        return Metrics{
            .group = group,
            .notif = notif,
            .publish = publish,
            .bad = bad,
            .fail = fail,
        };
    }

    void addNotif() const { METRIC_INCR(group, notif, 1); }
    void addPublished() const { METRIC_INCR(group, publish, 1); }
    void addBad() const { METRIC_INCR(group, bad, 1); }
    void addFail() const { METRIC_INCR(group, fail, 1); }

    GroupHandle group;
    CounterHandle notif;
    CounterHandle publish;
    CounterHandle bad;
    CounterHandle fail;

    static constexpr auto component = MetricsBackend::MetricComponent::Wheel;
};

inline Metrics &metrics() {
    static Metrics instance = Metrics::create();
    return instance;
}

} // namespace Totem::Wheel::detail
