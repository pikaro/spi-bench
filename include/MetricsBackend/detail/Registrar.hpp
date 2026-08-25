#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "MetricsBackend/detail/Types.hpp" // IWYU pragma: keep
#include "Services/Metrics.hpp"
#include "Store.hpp"
#include "Types/Error.hpp"
#include <expected>
#include <functional>

namespace Totem::MetricsBackend::detail {

class Registrar : public IRegistrar {
  public:
    explicit Registrar(Store &store) : _store(store) {}

    std::expected<GroupHandle, ReturnCode>
    // Take a reference wrapper to reject temporaries
    addGroup(std::reference_wrapper<const MetricGroupDesc> desc,
             bool enabled) override {
        const auto &descRef = desc.get();
        if (!enabled) {
            return GroupHandle::null();
        }
        FAIL_IF_ERR_FWD_UNEXPECTED(
            descRef.validate(),
            "Invalid metric group description for metric group %s:",
            descRef.name);
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            groupKey, _store.addMetricGroup(descRef.name, desc),
            "Failed to add metric group %s:", descRef.name);
        return GroupHandle::make(groupKey);
    }

    std::expected<CounterHandle, ReturnCode>
    addCounter(MetricGroupKey groupKey,
               std::reference_wrapper<const MetricDesc> desc,
               bool enabled) override {
        if (!enabled) {
            return CounterHandle::null();
        }
        const auto &descRef = desc.get();
        FAIL_IF(descRef.type != MetricType::Counter,
                std::unexpected(ERR(InvalidArgument)),
                "Metric type must be Counter for counter metrics");
        FAIL_IF_ERR_FWD_UNEXPECTED(
            descRef.validate(),
            "Invalid metric description for counter metric %s:", descRef.name);
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            metricKey, _store.addMetric(descRef.name, groupKey, desc),
            "Failed to add counter metric %s:", descRef.name);
        return CounterHandle::make(metricKey);
    }

    std::expected<GaugeHandle, ReturnCode>
    addGauge(MetricGroupKey groupKey,
             std::reference_wrapper<const MetricDesc> desc,
             bool enabled) override {
        if (!enabled) {
            return GaugeHandle::null();
        }
        const auto &descRef = desc.get();
        FAIL_IF(descRef.type != MetricType::Gauge,
                std::unexpected(ERR(InvalidArgument)),
                "Metric type must be Gauge for gauge metrics");
        FAIL_IF_ERR_FWD_UNEXPECTED(
            descRef.validate(),
            "Invalid metric description for gauge metric %s:", descRef.name);
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            metricKey, _store.addMetric(descRef.name, groupKey, desc),
            "Failed to add gauge metric %s:", descRef.name);
        return GaugeHandle::make(metricKey);
    }

    std::expected<SignedGaugeHandle, ReturnCode>
    addSignedGauge(MetricGroupKey groupKey,
                   std::reference_wrapper<const MetricDesc> desc,
                   bool enabled) override {
        if (!enabled) {
            return SignedGaugeHandle::null();
        }
        const auto &descRef = desc.get();
        FAIL_IF(descRef.type != MetricType::SignedGauge,
                std::unexpected(ERR(InvalidArgument)),
                "Metric type must be SignedGauge for signed gauge metrics");
        FAIL_IF_ERR_FWD_UNEXPECTED(
            descRef.validate(),
            "Invalid metric description for signed gauge metric %s:",
            descRef.name);
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            metricKey, _store.addMetric(descRef.name, groupKey, desc),
            "Failed to add signed gauge metric %s:", descRef.name);
        return SignedGaugeHandle::make(metricKey);
    }

  private:
    Store &_store;
};

} // namespace Totem::MetricsBackend::detail
