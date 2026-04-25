#pragma once

#include "Generic/Directory.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "MetricsBackend/detail/MetricGroupDirectory.hpp"
#include "MetricsBackend/detail/Types.hpp"
#include "StaticConfig/Metrics.hpp"
#include "Types/Error.hpp"
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <expected>

namespace Totem::MetricsBackend::detail {

struct MetricSlot {
    const MetricDesc *desc;
    MetricGroupKey groupKey;
    uint32_t value;
};

class MetricDirectory;

using MetricDirectoryImpl =
    BaseGettableDirectory<MetricDirectory, MetricKey, MetricSlot,
                          MetricConfig::maxMetrics>;

class MetricDirectory : public MetricDirectoryImpl {
  public:
    explicit MetricDirectory(MetricGroupDirectory &groupDirectory)
        : MetricDirectoryImpl("MetricDirectory",
                              Totem::MetricsBackend::detail::logComponent),
          _groupDirectory(groupDirectory) {}

    std::expected<MetricKey, ReturnCode> add(MetricKey metricKey,
                                             MetricGroupKey groupKey,
                                             const MetricDesc &metricDesc) {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            key,
            _addImpl(metricKey,
                     {.desc = &metricDesc, .groupKey = groupKey, .value = 0}),
            "Failed to add metric with key %" PRIuPTR, metricKey);
        FAIL_IF_ERR_FWD_UNEXPECTED(_groupDirectory.addMetricToGroup(groupKey),
                                   "Failed to add metric with key %" PRIuPTR
                                   " to group with key %" PRIuPTR,
                                   metricKey, groupKey);
        return key;
    }

    template <typename Fn, typename Filter>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const MetricKey &,
                                       const Metric &>)
    ReturnCode withAllInGroupConst(Fn &&fn,
                                   const MetricGroupKey &groupKey) const {
        return withAllConst(
            [&](const MetricKey &key, const MetricSlot &metricSlot) {
                return fn(key,
                          {.desc = metricSlot.desc, .value = metricSlot.value});
            },
            [&](const MetricKey & /*unused*/, const MetricSlot &metric) {
                return metric.groupKey == groupKey;
            });
    }

  private:
    MetricGroupDirectory &_groupDirectory;
};

} // namespace Totem::MetricsBackend::detail
