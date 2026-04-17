#pragma once

#include "Generic/Directory.hh"
#include "Macros/Facade.hh"
#include "MetricsBackend/detail/MetricGroupDirectory.hh"
#include "MetricsBackend/detail/Types.hh"
#include "StaticConfig/Metrics.hh"
#include "Types/Error.hh"
#include "Types/Metrics.hh"
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <expected>

namespace Totem::MetricsBackend::detail {

using MetricDirectoryImpl =
    GettableDirectory<uintptr_t, Metric, MetricConfig::maxMetrics>;

class MetricDirectory : public MetricDirectoryImpl {
    using MetricGroupKey = MetricGroupDirectory::EntryKey;

  public:
    using EntryKey = typename MetricDirectoryImpl::EntryKey;

    explicit MetricDirectory(MetricGroupDirectory &groupDirectory)
        : MetricDirectoryImpl("MetricDirectory",
                              Totem::MetricsBackend::detail::logComponent),
          _groupDirectory(groupDirectory) {
        _setHooks({
            .self = this,
        });
    }

    std::expected<EntryKey, ReturnCode> add(EntryKey metricKey,
                                            const MetricDesc &metricDesc) {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            key, _addImpl(metricKey, {.desc = metricDesc}),
            "Failed to add metric with key %" PRIuPTR, metricKey);
        FAIL_IF_ERR_FWD_UNEXPECTED(
            _groupDirectory.addMetricToGroup(metricDesc.group),
            "Failed to add metric with key %" PRIuPTR
            " to group with key %" PRIuPTR,
            metricKey, metricDesc.group);
        return key;
    }

    template <typename Fn, typename Filter>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const EntryKey &,
                                       const Metric &>)
    ReturnCode withAllInGroupConst(Fn &&fn,
                                   const MetricGroupKey &groupKey) const {
        return withAllConst(
            [&](const EntryKey &key, const Metric &metric) {
                return fn(key, metric);
            },
            [&](const EntryKey & /*unused*/, const Metric &metric) {
                return metric.desc.group == groupKey;
            });
    }

  private:
    MetricGroupDirectory &_groupDirectory;
};

} // namespace Totem::MetricsBackend::detail
