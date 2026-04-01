#pragma once

#include "Generic/Directory.hh"
#include "Metrics/detail/Types.hh"
#include "StaticConfig/Metrics.hh"
#include "Types/Error.hh"
#include "Types/Metrics.hh"
#include <cstring>
#include <expected>

namespace Totem::Metrics::detail {

using MetricDirectoryImpl =
    Generic::Directory<Metric, MetricConfig::maxMetrics,
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

} // namespace Totem::Metrics::detail
