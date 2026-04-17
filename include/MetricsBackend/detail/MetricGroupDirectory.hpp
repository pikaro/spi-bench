#pragma once

#include "Generic/Directory.hh"
#include "Macros/Facade.hh"
#include "MetricsBackend/detail/Types.hh"
#include "StaticConfig/Metrics.hh"
#include "Types/Error.hh"
#include "Types/Metrics.hh"
#include <cstdint>
#include <cstring>
#include <expected>

namespace Totem::MetricsBackend::detail {

using MetricGroupDirectoryImpl =
    GettableDirectory<uintptr_t, MetricGroup, MetricConfig::maxMetricGroups>;

class MetricGroupDirectory : public MetricGroupDirectoryImpl {
  public:
    using EntryKey = typename MetricGroupDirectoryImpl::EntryKey;

    explicit MetricGroupDirectory()
        : MetricGroupDirectoryImpl(
              "MetricGroupDirectory",
              Totem::MetricsBackend::detail::logComponent) {
        _setHooks({
            .self = this,
        });
    }

    std::expected<EntryKey, ReturnCode>
    add(EntryKey metricGroupNameKey, const MetricGroupDesc &metricGroupDesc) {
        return _addImpl(metricGroupNameKey, {.desc = metricGroupDesc});
    }

    ReturnCode addMetricToGroup(const EntryKey &groupKey) {
        return withEntry(groupKey, [&](MetricGroup &group) -> ReturnCode {
            if (group.metricCount >= MetricConfig::maxMetricsPerGroup) {
                return ERR(Overflow);
            }
            group.metricCount++;
            return OK();
        });
    }
};

} // namespace Totem::MetricsBackend::detail
