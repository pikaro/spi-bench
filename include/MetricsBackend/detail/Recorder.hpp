#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "MetricsBackend/detail/MetricDirectory.hpp"
#include "MetricsBackend/detail/Types.hpp" // IWYU pragma: keep
#include "Services/Metrics.hpp"
#include "Store.hpp"
#include "Types/Error.hpp"
#include <bit>
#include <cstdint>

namespace Totem::MetricsBackend::detail {

class Recorder : public IRecorder {
  public:
    explicit Recorder(Store &store) : _store(store) {}

    ReturnCode increment(CounterHandle handle, uint32_t value = 1) override {
        FAIL_IF(handle == CounterHandle::null(), ERR(InvalidArgument),
                "Cannot increment null counter handle");
        return _store.incrementMetric(handle.key(), value);
    }

    ReturnCode decrement(CounterHandle handle, uint32_t value = 1) override {
        FAIL_IF(handle == CounterHandle::null(), ERR(InvalidArgument),
                "Cannot decrement null counter handle");
        return _store.decrementMetric(handle.key(), value);
    }

    ReturnCode set(GaugeHandle handle, uint32_t value) override {
        FAIL_IF(handle == GaugeHandle::null(), ERR(InvalidArgument),
                "Cannot set null gauge handle");
        return _store.setMetric(handle.key(), value);
    }

    ReturnCode set(SignedGaugeHandle handle, int32_t value) override {
        FAIL_IF(handle == SignedGaugeHandle::null(), ERR(InvalidArgument),
                "Cannot set null signed gauge handle");
        return _store.setMetric(handle.key(), std::bit_cast<uint32_t>(value));
    }

  private:
    Store &_store;
};

} // namespace Totem::MetricsBackend::detail
