#pragma once

#include "MetricsBackend/Interfaces/Types.hpp"
#include "StaticConfig/Metrics.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstdint>

namespace Totem::MetricsBackend {

using GroupMetricsArray =
    std::array<MetricsBackend::Metric, MetricConfig::maxMetricsPerGroup>;

struct MetricFrame {
    uint32_t timestampMs = 0;
    MetricsBackend::MetricGroup group;
    GroupMetricsArray metrics;
};

struct IFrameSink {
    virtual ~IFrameSink() = default;

    virtual ReturnCode consume(const MetricFrame &metricFrame) = 0;
};

} // namespace Totem::MetricsBackend
