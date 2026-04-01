#pragma once

#include "Macros/Facade.hh"
#include "Metrics/detail/Types.hh"
#include "Store.hh"
#include "Types/Error.hh"
#include "Types/Metrics.hh"
#include <expected>

namespace Totem::Metrics::detail {

class Registrar {
  public:
    explicit Registrar(Store &store) : _store(store) {}

    std::expected<GroupHandle, ReturnCode>
    addGroup(const MetricGroupDesc &desc) {
        FAIL_IF_ERR_FWD_UNEXPECTED(
            desc.validate(),
            "Invalid metric group description for metric group %s:", desc.name);
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            nameKey, _store.addMetricGroup(desc.name, desc),
            "Failed to add metric group %s:", desc.name);
        return GroupHandle::make(nameKey);
    }

    std::expected<CounterHandle, ReturnCode>
    addCounter(const MetricDesc &desc) {
        FAIL_IF(desc.type != MetricType::Counter,
                std::unexpected(ERR(InvalidArgument)),
                "Metric type must be Counter for counter metrics");
        FAIL_IF_ERR_FWD_UNEXPECTED(
            desc.validate(),
            "Invalid metric description for counter metric %s:", desc.name);
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            nameKey, _store.addMetric(desc.name, desc),
            "Failed to add counter metric %s:", desc.name);
        return CounterHandle::make(nameKey);
    }

    std::expected<GaugeHandle, ReturnCode> addGauge(const MetricDesc &desc) {
        FAIL_IF(desc.type != MetricType::Gauge,
                std::unexpected(ERR(InvalidArgument)),
                "Metric type must be Gauge for gauge metrics");
        FAIL_IF_ERR_FWD_UNEXPECTED(
            desc.validate(),
            "Invalid metric description for gauge metric %s:", desc.name);
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            nameKey, _store.addMetric(desc.name, desc),
            "Failed to add gauge metric %s:", desc.name);
        return GaugeHandle::make(nameKey);
    }

  private:
    Store &_store;

    using DefaultError = CoreError;
};

} // namespace Totem::Metrics::detail
