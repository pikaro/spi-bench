#pragma once

#include "Generic/Directory.hh"
#include "MetricsBackend/detail/Types.hh"
#include "StaticConfig/Metrics.hh"
#include "Types/Error.hh"
#include "Types/Metrics.hh"
#include <cstring>
#include <expected>

namespace Totem::MetricsBackend::detail {

using MetricGroupDirectoryImpl =
    Generic::Directory<MetricGroup, MetricConfig::maxMetricGroups,
                       MetricConfig::maxMetricGroupNameLength>;

class MetricGroupDirectory : public MetricGroupDirectoryImpl {
  public:
    explicit MetricGroupDirectory()
        : MetricGroupDirectoryImpl("MetricGroupDirectory") {
        _setHooks({
            .self = this,
        });
    }

    std::expected<EntryNameKey, ReturnCode>
    add(const EntryNameKey &metricGroupNameKey,
        const MetricGroupDesc &metricGroupDesc) {
        return _addImpl(metricGroupNameKey, {.desc = metricGroupDesc});
    }
};

} // namespace Totem::MetricsBackend::detail
