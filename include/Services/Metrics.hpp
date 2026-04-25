#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/IFrameSink.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include <cstdint>
#include <expected>
#include <functional>

namespace Totem::MetricsBackend::detail {

struct IRegistrar {
    virtual ~IRegistrar() = default;

    virtual std::expected<GroupHandle, ReturnCode>
    addGroup(std::reference_wrapper<const MetricGroupDesc> groupDesc) = 0;

    virtual std::expected<CounterHandle, ReturnCode>
    addCounter(MetricGroupKey groupKey,
               std::reference_wrapper<const MetricDesc> metricDesc) = 0;

    virtual std::expected<GaugeHandle, ReturnCode>
    addGauge(MetricGroupKey groupKey,
             std::reference_wrapper<const MetricDesc> metricDesc) = 0;
};

struct IRecorder {
    virtual ~IRecorder() = default;

    virtual ReturnCode increment(CounterHandle handle, uint32_t value = 1) = 0;
    virtual ReturnCode decrement(CounterHandle handle, uint32_t value = 1) = 0;
    virtual ReturnCode set(GaugeHandle handle, uint32_t value) = 0;
};

struct IMetrics {
    virtual ~IMetrics() = default;

    virtual ReturnCode snapshot(IFrameSink &, const char *) = 0;
};

} // namespace Totem::MetricsBackend::detail

class MetricsService {
    using IRegistrar = Totem::MetricsBackend::detail::IRegistrar;
    using IRecorder = Totem::MetricsBackend::detail::IRecorder;
    using IMetrics = Totem::MetricsBackend::detail::IMetrics;

  public:
    static void set(IMetrics &backend) { _backend = &backend; }
    static void setRecorder(IRecorder &recorder) { _recorder = &recorder; }
    static void setRegistrar(IRegistrar &registrar) { _registrar = &registrar; }

    static IMetrics &get() {
        ABORT_IF_NULL(_backend, "Metrics backend not bound");
        return *_backend;
    }

    static IRegistrar &registrar() {
        ABORT_IF_NULL(_backend, "Metrics backend not bound");
        return *_registrar;
    }

    static IRecorder &recorder() {
        ABORT_IF_NULL(_backend, "Metrics backend not bound");
        return *_recorder;
    }

  private:
    static inline IMetrics *_backend = nullptr;
    static inline IRegistrar *_registrar = nullptr;
    static inline IRecorder *_recorder = nullptr;
};
