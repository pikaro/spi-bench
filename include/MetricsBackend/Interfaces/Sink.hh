#pragma once

#include "MetricsBackend/detail/Types.hh"
#include "StaticConfig/Metrics.hh"
#include "Types/Error.hh"
#include <array>
#include <cstdint>

namespace Totem::MetricsBackend {

using GroupMetricsArray = std::array<MetricsBackend::detail::Metric,
                                     MetricConfig::maxMetricsPerGroup>;

struct MetricFrame {
    uint32_t timestampMs = 0;
    MetricsBackend::detail::MetricGroup group;
    GroupMetricsArray metrics;
};

struct Sink {
    void *self = nullptr;

    ReturnCode (*consumeHook)(void *, const MetricFrame &) = nullptr;

    ReturnCode consume(const MetricFrame &metricSnapshot) const {
        return consumeHook(self, metricSnapshot);
    }

    [[nodiscard]] bool validate() const {
        return self != nullptr && consumeHook != nullptr;
    }
};

} // namespace Totem::MetricsBackend
