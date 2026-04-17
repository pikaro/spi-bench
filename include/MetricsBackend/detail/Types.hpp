#pragma once

#include "Types/Collection.hpp"
#include "Types/Logging.hpp"
#include "Types/Metrics.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Totem::MetricsBackend::detail {

inline constexpr LogComponent logComponent = LogComponent::Metrics;

struct MetricGroup {
    const MetricGroupDesc *desc{};
    size_t metricCount = 0;

    [[nodiscard]] std::string_view displayName() const { return desc->name; }
};

struct Metric {
    const MetricDesc *desc{};
    uint32_t value = 0;

    [[nodiscard]] std::string_view displayName() const { return desc->name; }
    [[nodiscard]] std::string_view unit() const { return desc->unitString(); }
};

struct GroupTag {};
struct CounterTag {};
struct GaugeTag {};

class Registrar;

using GroupHandle = StrongHandle<GroupTag, uintptr_t, Registrar>;
using CounterHandle = StrongHandle<CounterTag, uintptr_t, Registrar>;
using GaugeHandle = StrongHandle<GaugeTag, uintptr_t, Registrar>;

} // namespace Totem::MetricsBackend::detail
