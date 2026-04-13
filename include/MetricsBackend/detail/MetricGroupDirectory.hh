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
    Directory<uintptr_t, MetricGroup, MetricConfig::maxMetricGroups>;

class MetricGroupDirectory : public MetricGroupDirectoryImpl {
  public:
    using EntryKey = typename MetricGroupDirectoryImpl::EntryKey;

    explicit MetricGroupDirectory()
        : MetricGroupDirectoryImpl("MetricGroupDirectory") {
        _setHooks({
            .self = this,
        });
    }

    std::expected<EntryKey, ReturnCode>
    add(EntryKey metricGroupNameKey,
        const MetricGroupDesc &metricGroupDesc) {
        return _addImpl(metricGroupNameKey, {.desc = metricGroupDesc});
    }
};

} // namespace Totem::MetricsBackend::detail
