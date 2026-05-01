#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/IFrameSink.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "StaticConfig/Metrics.hpp"
#include "Types/Error.hpp"
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <utility>

namespace Totem::MetricsBackend::detail {

struct IRegistrar {
    virtual ~IRegistrar() = default;

    virtual std::expected<GroupHandle, ReturnCode>
    addGroup(std::reference_wrapper<const MetricGroupDesc> groupDesc,
             bool enabled) = 0;

    virtual std::expected<CounterHandle, ReturnCode>
    addCounter(MetricGroupKey groupKey,
               std::reference_wrapper<const MetricDesc> metricDesc,
               bool enabled) = 0;

    virtual std::expected<GaugeHandle, ReturnCode>
    addGauge(MetricGroupKey groupKey,
             std::reference_wrapper<const MetricDesc> metricDesc,
             bool enabled) = 0;
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

constexpr std::optional<Totem::MetricsBackend::MetricLevel>
level_for_metric_component_opt(
    Totem::MetricsBackend::MetricComponent metricComponent) {
    using MetricComponent = Totem::MetricsBackend::MetricComponent;
    switch (metricComponent) {
    case MetricComponent::PubSub:
        return MetricCollection::pubSub;
    case MetricComponent::Rs485:
        return MetricCollection::rs485;
    case MetricComponent::Spi:
        return MetricCollection::spi;
    case MetricComponent::TaskController:
        return MetricCollection::taskController;
    case MetricComponent::TaskControllerRegistry:
        return MetricCollection::taskControllerRegistry;
    case MetricComponent::Logging:
        return MetricCollection::logging;
    case MetricComponent::Mutex:
        return MetricCollection::mutex;
    default:
        std::unreachable();
    }
}

constexpr Totem::MetricsBackend::MetricLevel level_for_metric_component(
    Totem::MetricsBackend::MetricComponent metricComponent) {
    return level_for_metric_component_opt(metricComponent)
        .value_or(MetricCollection::minimum);
}

constexpr bool
metrics_enabled(Totem::MetricsBackend::MetricComponent metricComponent,
                Totem::MetricsBackend::MetricGroupDesc groupDesc) {
    return static_cast<uint8_t>(groupDesc.level) >=
           static_cast<uint8_t>(level_for_metric_component(metricComponent));
}

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
        ABORT_IF_NULL(_registrar, "Metrics registrar not bound");
        return *_registrar;
    }

    static IRecorder &recorder() {
        ABORT_IF_NULL(_recorder, "Metrics recorder not bound");
        return *_recorder;
    }

  private:
    static inline IMetrics *_backend = nullptr;
    static inline IRegistrar *_registrar = nullptr;
    static inline IRecorder *_recorder = nullptr;
};
