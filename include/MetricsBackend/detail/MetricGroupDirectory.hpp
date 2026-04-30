#pragma once

#include "Generic/Directory.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "MetricsBackend/detail/Types.hpp"
#include "StaticConfig/Metrics.hpp"
#include "Types/Error.hpp"
#include <cstring>
#include <expected>

namespace Totem::MetricsBackend::detail {

class MetricGroupDirectory;

using MetricGroupDirectoryImpl =
    BaseGettableDirectory<MetricGroupDirectory, MetricGroupKey, MetricGroup,
                          MetricConfig::maxMetricGroups>;

class MetricGroupDirectory : public MetricGroupDirectoryImpl {
  public:
    static constexpr LogComponent logComponent =
        Totem::MetricsBackend::detail::logComponent;

    explicit MetricGroupDirectory() : MetricGroupDirectoryImpl(
                                          "MetricGroupDirectory") {}

    std::expected<MetricGroupKey, ReturnCode>
    add(MetricGroupKey metricGroupNameKey,
        const MetricGroupDesc &metricGroupDesc) {
        return _addImpl(metricGroupNameKey, {.desc = &metricGroupDesc});
    }

    ReturnCode addMetricToGroup(const MetricGroupKey &groupKey) {
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
