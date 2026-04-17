#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/detail/MetricDirectory.hpp"
#include "MetricsBackend/detail/Types.hpp"
#include "Store.hpp"
#include "Types/Error.hpp"
#include <cstdint>

namespace Totem::MetricsBackend::detail {

class Recorder {
  public:
    explicit Recorder(Store &store) : _store(store) {}

    ReturnCode increment(CounterHandle handle, uint32_t value = 1) {
        return _store.withMetric(handle.key(),
                                 [value](MetricSlot &metric) -> ReturnCode {
                                     metric.value += value;
                                     return OK();
                                 });
    }

    ReturnCode decrement(CounterHandle handle, uint32_t value = 1) {
        return _store.withMetric(handle.key(),
                                 [value](MetricSlot &metric) -> ReturnCode {
                                     metric.value -= value;
                                     return OK();
                                 });
    }

    ReturnCode set(GaugeHandle handle, uint32_t value) {
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
