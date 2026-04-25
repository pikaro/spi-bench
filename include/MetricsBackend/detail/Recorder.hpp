#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "MetricsBackend/detail/MetricDirectory.hpp"
#include "MetricsBackend/detail/Types.hpp" // IWYU pragma: keep
#include "Services/Metrics.hpp"
#include "Store.hpp"
#include "Types/Error.hpp"
#include <cstdint>

namespace Totem::MetricsBackend::detail {

class Recorder : public IRecorder {
  public:
    explicit Recorder(Store &store) : _store(store) {}

    ReturnCode increment(CounterHandle handle, uint32_t value = 1) override {
        return _store.withMetric(handle.key(),
                                 [value](MetricSlot &metric) -> ReturnCode {
                                     metric.value += value;
                                     return OK();
                                 });
    }

    ReturnCode decrement(CounterHandle handle, uint32_t value = 1) override {
        return _store.withMetric(handle.key(),
                                 [value](MetricSlot &metric) -> ReturnCode {
                                     metric.value -= value;
                                     return OK();
                                 });
    }

    ReturnCode set(GaugeHandle handle, uint32_t value) override {
        return _store.withMetric(handle.key(),
                                 [value](MetricSlot &metric) -> ReturnCode {
                                     metric.value = value;
                                     return OK();
                                 });
    }

  private:
    Store &_store;
};

} // namespace Totem::MetricsBackend::detail
