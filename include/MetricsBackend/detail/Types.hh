#pragma once

#include "StaticConfig/Metrics.hh"
#include "Types/Collection.hh"
#include "Types/Metrics.hh"
#include <cstddef>
#include <cstdint>

namespace Totem::MetricsBackend::detail {

struct MetricGroup {
    MetricGroupDesc desc{};
    size_t metricCount = 0;
};

struct Metric {
    MetricDesc desc{};
    uint32_t value = 0;
};

struct GroupTag {};
struct CounterTag {};
struct GaugeTag {};

class Registrar;

using GroupHandle =
    StrongHandle<GroupTag, MetricConfig::maxMetricGroupNameLength, Registrar>;
using CounterHandle =
    StrongHandle<CounterTag, MetricConfig::maxMetricNameLength, Registrar>;
using GaugeHandle =
    StrongHandle<GaugeTag, MetricConfig::maxMetricNameLength, Registrar>;

} // namespace Totem::MetricsBackend::detail
