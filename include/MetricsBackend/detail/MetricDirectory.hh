#pragma once

#include "Generic/Directory.hh"
#include "MetricsBackend/detail/Types.hh"
#include "StaticConfig/Metrics.hh"
#include "Types/Error.hh"
#include "Types/Metrics.hh"
#include <cstring>
#include <expected>

namespace Totem::MetricsBackend::detail {

using MetricDirectoryImpl = Directory<Metric, MetricConfig::maxMetrics,
                                      MetricConfig::maxMetricNameLength>;

class MetricDirectory : public MetricDirectoryImpl {
  public:
    explicit MetricDirectory() : MetricDirectoryImpl("MetricDirectory") {
        _setHooks({
            .self = this,
        });
    }

    std::expected<EntryNameKey, ReturnCode>
    add(const EntryNameKey &metricNameKey, const MetricDesc &metricDesc) {
        return _addImpl(metricNameKey, {.desc = metricDesc});
    }
};

} // namespace Totem::MetricsBackend::detail
