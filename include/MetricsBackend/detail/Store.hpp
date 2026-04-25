#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/IFrameSink.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "MetricsBackend/detail/MetricDirectory.hpp"
#include "MetricsBackend/detail/MetricGroupDirectory.hpp"
#include "MetricsBackend/detail/Types.hpp" // IWYU pragma: keep
#include "Types/Error.hpp"
#include <cstring>
#include <expected>

namespace Totem::MetricsBackend::detail {

class Store {
  public:
    Store() : _metricDirectory(_metricGroupDirectory) { _enableRegistration(); }
    ~Store() { _disableRegistration(); }

    using MetricGroupKeySnapshot = MetricGroupDirectory::EntryKeySnapshot;
    using MetricGroupSnapshot = MetricGroupDirectory::EntrySnapshot;

    using MetricKeySnapshot = MetricDirectory::EntrySnapshot;
    using MetricSnapshot = MetricDirectory::EntrySnapshot;

    static constexpr const char *name = "Metrics::Store";

    struct GroupMetricSnapshot {
        GroupMetricsArray metrics{};
        size_t count = 0;
    };

    std::expected<MetricGroupKey, ReturnCode>
    addMetricGroup(const char *metricGroupName,
                   const MetricGroupDesc &metricGroupDesc) {
        FAIL_IF_NULL(metricGroupName, std::unexpected(ERR(InvalidArgument)),
                     "Cannot register metric group with null name");
        _log_i("Registering metric group %s", metricGroupName);
        return _metricGroupDirectory.add(_nextMetricGroupKey++,
                                         metricGroupDesc);
    }

    std::expected<MetricKey, ReturnCode>
    addMetric(const char *metricName, MetricGroupKey groupKey,
              const MetricDesc &metricDesc) {
        FAIL_IF_NULL(metricName, std::unexpected(ERR(InvalidArgument)),
                     "Cannot register metric with null name");
        _log_i("Registering metric %s", metricName);
        return _metricDirectory.add(_nextMetricKey++, groupKey, metricDesc);
    }

    template <typename Fun>
        requires(std::is_invocable_r_v<ReturnCode, Fun, MetricSlot &>)
    ReturnCode withMetric(MetricKey metricKey, Fun fun) {
        return _metricDirectory.withEntry(metricKey, fun);
    }

    [[nodiscard]] std::expected<GroupMetricSnapshot, ReturnCode>
    getMetricsForGroup(MetricGroupKey groupKey) const {
        GroupMetricSnapshot out{};
        auto ret = _metricDirectory.withAllConst(
            [&out](const MetricKey &, const MetricSlot &metric) -> ReturnCode {
                FAIL_IF(out.count >= out.metrics.size(), ERR(Overflow),
                        "Metric group snapshot buffer is full");
                out.metrics[out.count++] = {.desc = metric.desc,
                                            .value = metric.value};
                return OK();
            },
            [groupKey](const MetricKey &, const MetricSlot &metricSlot) {
                return metricSlot.groupKey == groupKey;
            });
        FAIL_IF_ERR_FWD_UNEXPECTED(
            ret, "Failed to get metrics for group key %u", groupKey);
        return out;
    }

    std::expected<MetricGroupKey, ReturnCode>
    getGroupKeyByName(const char *metricGroupName) const {
        auto ret = _metricGroupDirectory.snapshotKeys(
            [&](const auto & /*unused*/, const auto &entry) {
                return (std::strcmp(entry.desc->name, metricGroupName) == 0);
            },
            {.min = 1, .max = 1});
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            keys, ret, "Failed to find metric group with name %s",
            metricGroupName);
        return keys.keys[0];
    }

    std::expected<GroupMetricSnapshot, ReturnCode>
    getMetricsForGroup(const char *metricGroupName) const {
        auto groupKeyResult = getGroupKeyByName(metricGroupName);
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            groupKey, groupKeyResult,
            "Failed to get group key for metric group name %s",
            metricGroupName);
        return getMetricsForGroup(groupKey);
    }

    [[nodiscard]] std::expected<MetricGroupKeySnapshot, ReturnCode>
    getAllGroupKeys() const {
        return _metricGroupDirectory.snapshotKeys();
    }

    [[nodiscard]] std::expected<MetricGroup, ReturnCode>
    getMetricGroup(MetricGroupKey groupKey) const {
        return _metricGroupDirectory.getCopy(groupKey);
    }

    [[nodiscard]] std::expected<Metric, ReturnCode>
    getMetric(MetricKey metricKey) const {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            metricSlot, _metricDirectory.getCopy(metricKey),
            "Failed to get metric for metric key %u", metricKey);
        return Metric{.desc = metricSlot.desc, .value = metricSlot.value};
    }

  private:
    void _disableRegistration() {
        _metricDirectory.disableRegistration();
        _metricGroupDirectory.disableRegistration();
    }
    void _enableRegistration() {
        _metricDirectory.enableRegistration();
        _metricGroupDirectory.enableRegistration();
    }

    MetricGroupDirectory _metricGroupDirectory;
    MetricDirectory _metricDirectory;
    MetricGroupKey _nextMetricGroupKey = 1;
    MetricKey _nextMetricKey = 1;
};

} // namespace Totem::MetricsBackend::detail
