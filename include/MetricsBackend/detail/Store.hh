#pragma once

#include "Macros/Facade.hh"
#include "MetricsBackend/detail/MetricDirectory.hh"
#include "MetricsBackend/detail/MetricGroupDirectory.hh"
#include "MetricsBackend/detail/Types.hh"
#include "StaticConfig/Metrics.hh"
#include "Types/Error.hh"
#include "Types/Metrics.hh"
#include <array>
#include <expected>
#include <optional>

namespace Totem::MetricsBackend::detail {

class Store {
  public:
    Store() { _enableRegistration(); }
    ~Store() { _disableRegistration(); }

    using MetricGroupNameKey = MetricGroupDirectory::EntryNameKey;
    using MetricNameKey = MetricDirectory::EntryNameKey;

    static constexpr const char *name = "Metrics::Store";

    std::expected<MetricGroupNameKey, ReturnCode>
    addMetricGroup(const char *metricGroupName,
                   const MetricGroupDesc &metricGroupDesc) {
        FAIL_IF_NULL(metricGroupName, std::unexpected(ERR(InvalidArgument)),
                     "Cannot register metric group with null name");
        _log_i("Registering metric group %s", metricGroupName);
        return _metricGroupDirectory.add(
            MetricGroupNameKey::fromCharPtr(metricGroupName), metricGroupDesc);
    }

    std::expected<MetricNameKey, ReturnCode>
    addMetric(const char *metricName, const MetricDesc &metricDesc) {
        FAIL_IF_NULL(metricName, std::unexpected(ERR(InvalidArgument)),
                     "Cannot register metric with null name");
        _log_i("Registering metric %s in group %s", metricName,
               metricDesc.group.name.data());
        return _metricDirectory.add(MetricNameKey::fromCharPtr(metricName),
                                    metricDesc);
    }

    template <typename Fun>
        requires(std::is_invocable_r_v<ReturnCode, Fun, Metric &>)
    ReturnCode withMetric(MetricNameKey metricNameKey, Fun fun) {
        return _metricDirectory.withEntry(metricNameKey, fun);
    }

    [[nodiscard]] std::expected<
        std::array<Metric, MetricConfig::maxMetricsPerGroup>, ReturnCode>
    getMetricsForGroup(MetricGroupNameKey groupNameKey) const {
        std::array<Metric, MetricConfig::maxMetricsPerGroup> out{};
        auto ret = _metricDirectory.withAllConst(
            [&out](const MetricNameKey & /*unused*/, const Metric &metric) {
                out[out.size()] = metric;
                return OK();
            },
            [&groupNameKey](const MetricNameKey & /*unused*/,
                            const Metric &metric) {
                return metric.desc.group == groupNameKey;
            });
        FAIL_IF_ERR(ret, std::unexpected(ret),
                    "Failed to get metrics for group %s",
                    groupNameKey.name.data());
        return out;
    }

    [[nodiscard]] std::expected<Metric, ReturnCode>
    getMetric(MetricNameKey metricNameKey) const {
        std::optional<Metric> out;
        auto ret = _metricDirectory.withEntryConst(
            metricNameKey, [&out](const Metric &metric) {
                out.emplace(metric);
                return OK();
            });
        FAIL_IF_ERR(ret, std::unexpected(ret), "Failed to get metric %s",
                    metricNameKey.name.data());
        FAIL_IF(!out.has_value(), std::unexpected(ERR(NotFound)),
                "Metric %s not found", metricNameKey.name.data());
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

    using DefaultError = CoreError;
};

} // namespace Totem::MetricsBackend::detail
