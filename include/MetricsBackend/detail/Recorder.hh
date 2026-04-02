#pragma once

#include "Macros/Facade.hh"
#include "MetricsBackend/detail/Types.hh"
#include "Store.hh"
#include "Types/Error.hh"
#include <cstdint>

namespace Totem::MetricsBackend::detail {

class Recorder {
  public:
    explicit Recorder(Store &store) : _store(store) {}

    ReturnCode increment(CounterHandle handle, uint32_t value = 1) {
        return _store.withMetric(handle.key(),
                                 [value](Metric &metric) -> ReturnCode {
                                     metric.value += value;
                                     return OK();
                                 });
    }

    ReturnCode decrement(CounterHandle handle, uint32_t value = 1) {
        return _store.withMetric(handle.key(),
                                 [value](Metric &metric) -> ReturnCode {
                                     metric.value -= value;
                                     return OK();
                                 });
    }

    ReturnCode set(GaugeHandle handle, uint32_t value) {
        return _store.withMetric(handle.key(),
                                 [value](Metric &metric) -> ReturnCode {
                                     metric.value = value;
                                     return OK();
                                 });
    }

  private:
    Store &_store;

    using DefaultError = CoreError;
};

} // namespace Totem::MetricsBackend::detail
