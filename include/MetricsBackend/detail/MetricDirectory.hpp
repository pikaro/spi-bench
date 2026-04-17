#pragma once

#include "Generic/Directory.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/detail/MetricGroupDirectory.hpp"
#include "MetricsBackend/detail/Types.hpp"
#include "StaticConfig/Metrics.hpp"
#include "Types/Error.hpp"
#include "Types/Metrics.hpp"
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <expected>

namespace Totem::MetricsBackend::detail {

struct MetricSlot {
    const MetricDesc *desc;
    MetricGroupDirectory::EntryKey group;
    uint32_t value;
};

using MetricDirectoryImpl =
    GettableDirectory<uintptr_t, MetricSlot, MetricConfig::maxMetrics>;

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
                                            MetricGroupKey groupKey,
                                            const MetricDesc &metricDesc) {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            key,
            _addImpl(metricKey,
                     {.desc = &metricDesc, .group = groupKey, .value = 0}),
            "Failed to add metric with key %" PRIuPTR, metricKey);
        FAIL_IF_ERR_FWD_UNEXPECTED(_groupDirectory.addMetricToGroup(groupKey),
                                   "Failed to add metric with key %" PRIuPTR
                                   " to group with key %" PRIuPTR,
                                   metricKey, groupKey);
        return key;
    }

    template <typename Fn, typename Filter>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const EntryKey &,
                                       const Metric &>)
    ReturnCode withAllInGroupConst(Fn &&fn,
                                   const MetricGroupKey &groupKey) const {
        return withAllConst(
            [&](const EntryKey &key, const MetricSlot &metricSlot) {
                return fn(key,
                          {.desc = metricSlot.desc, .value = metricSlot.value});
            },
            [&](const EntryKey & /*unused*/, const MetricSlot &metric) {
                return metric.group == groupKey;
            });
    }

  private:
    MetricGroupDirectory &_groupDirectory;
};

} // namespace Totem::MetricsBackend::detail
