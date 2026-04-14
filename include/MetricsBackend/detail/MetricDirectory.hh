#pragma once

#include "Generic/Directory.hh"
#include "MetricsBackend/detail/Types.hh"
#include "StaticConfig/Metrics.hh"
#include "Types/Error.hh"
#include "Types/Metrics.hh"
#include <cstdint>
#include <cstring>
#include <expected>

namespace Totem::MetricsBackend::detail {

using MetricDirectoryImpl =
    Directory<uintptr_t, Metric, MetricConfig::maxMetrics>;

class MetricDirectory : public MetricDirectoryImpl {
  public:
    using EntryKey = typename MetricDirectoryImpl::EntryKey;

    explicit MetricDirectory() : MetricDirectoryImpl("MetricDirectory") {
        _setHooks({
            .self = this,
        });
    }

    std::expected<EntryKey, ReturnCode> add(EntryKey metricKey,
                                            const MetricDesc &metricDesc) {
        return _addImpl(metricKey, {.desc = metricDesc});
    }
};

} // namespace Totem::MetricsBackend::detail
