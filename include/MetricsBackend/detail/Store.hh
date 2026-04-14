#pragma once

#include "Macros/Facade.hh"
#include "MetricsBackend/detail/MetricDirectory.hh"
#include "MetricsBackend/detail/MetricGroupDirectory.hh"
#include "MetricsBackend/detail/Types.hh"
#include "StaticConfig/Metrics.hh"
#include "Types/Error.hh"
#include "Types/Metrics.hh"
#include <array>
#include <cstddef>
#include <expected>
#include <optional>

namespace Totem::MetricsBackend::detail {

class Store {
  public:
    Store() { _enableRegistration(); }
    ~Store() { _disableRegistration(); }

    using MetricGroupKey = MetricGroupDirectory::EntryKey;
    using MetricKey = MetricDirectory::EntryKey;

    static constexpr const char *name = "Metrics::Store";

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
    addMetric(const char *metricName, const MetricDesc &metricDesc) {
        FAIL_IF_NULL(metricName, std::unexpected(ERR(InvalidArgument)),
                     "Cannot register metric with null name");
        _log_i("Registering metric %s", metricName);
        return _metricDirectory.add(_nextMetricKey++, metricDesc);
    }

    template <typename Fun>
        requires(std::is_invocable_r_v<ReturnCode, Fun, Metric &>)
    ReturnCode withMetric(MetricKey metricKey, Fun fun) {
        return _metricDirectory.withEntry(metricKey, fun);
    }

    [[nodiscard]] std::expected<
        std::array<Metric, MetricConfig::maxMetricsPerGroup>, ReturnCode>
    getMetricsForGroup(MetricGroupKey groupKey) const {
        std::array<Metric, MetricConfig::maxMetricsPerGroup> out{};
        size_t count = 0;
        auto ret = _metricDirectory.withAllConst(
            [&out, &count](const MetricKey &, const Metric &metric) {
                FAIL_IF(count >= out.size(), ERR(OutOfMemory),
                        "Metric group result buffer is full");
                out[count++] = metric;
                return OK();
            },
            [&groupKey](const MetricKey &, const Metric &metric) {
                return metric.desc.group == groupKey;
            });
        FAIL_IF_ERR(ret, std::unexpected(ret),
                    "Failed to get metrics for group");
        return out;
    }

    [[nodiscard]] std::expected<Metric, ReturnCode>
    getMetric(MetricKey metricKey) const {
        std::optional<Metric> out;
        auto ret = _metricDirectory.withEntryConst(
            metricKey, [&out](const Metric &metric) {
                out.emplace(metric);
                return OK();
            });
        FAIL_IF_ERR(ret, std::unexpected(ret), "Failed to get metric");
        FAIL_IF(!out.has_value(), std::unexpected(ERR(NotFound)),
                "Metric not found");
        return *out;
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
