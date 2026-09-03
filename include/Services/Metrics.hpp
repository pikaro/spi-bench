#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/IFrameSink.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "StaticConfig/Metrics.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <magic_enum/magic_enum.hpp>
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

    virtual std::expected<SignedGaugeHandle, ReturnCode>
    addSignedGauge(MetricGroupKey groupKey,
                   std::reference_wrapper<const MetricDesc> metricDesc,
                   bool enabled) = 0;
};

struct IRecorder {
    virtual ~IRecorder() = default;

    virtual ReturnCode increment(CounterHandle handle, uint32_t value = 1) = 0;
    virtual ReturnCode decrement(CounterHandle handle, uint32_t value = 1) = 0;
    virtual ReturnCode set(GaugeHandle handle, uint32_t value) = 0;
    virtual ReturnCode set(SignedGaugeHandle handle, int32_t value) = 0;
};

struct IMetrics {
    virtual ~IMetrics() = default;

    virtual ReturnCode snapshot(IFrameSink &, const char *) = 0;
};

} // namespace Totem::MetricsBackend::detail

template <typename... Levels>
consteval auto make_metric_collection_levels(Levels... levels) {
    using MetricLevel = Totem::MetricsBackend::MetricLevel;
    using MetricComponent = Totem::MetricsBackend::MetricComponent;

    static_assert(sizeof...(levels) ==
                      magic_enum::enum_count<MetricComponent>(),
                  "MetricComponent changed; update metric collection mapping");
    return std::array<std::optional<MetricLevel>, sizeof...(levels)>{
        std::optional<MetricLevel>{levels}...};
}

template <size_t... Indices>
consteval bool
metric_component_values_are_dense(std::index_sequence<Indices...> /*unused*/) {
    using MetricComponent = Totem::MetricsBackend::MetricComponent;

    constexpr auto values = magic_enum::enum_values<MetricComponent>();
    return ((static_cast<size_t>(values[Indices]) == Indices) && ...);
}

static_assert(metric_component_values_are_dense(
                  std::make_index_sequence<magic_enum::enum_count<
                      Totem::MetricsBackend::MetricComponent>()>{}),
              "MetricComponent values must remain contiguous from zero");

inline constexpr auto metricCollectionLevels = make_metric_collection_levels(
    MetricCollection::pubSub, MetricCollection::taskControllerRegistry,
    MetricCollection::taskController, MetricCollection::rs485,
    MetricCollection::spi, MetricCollection::mutex, MetricCollection::logging,
    MetricCollection::audio, MetricCollection::i2c, MetricCollection::ledPwm,
    MetricCollection::input, MetricCollection::bluetooth,
    MetricCollection::wheel, MetricCollection::ledDisplay,
    MetricCollection::battery);

constexpr std::optional<Totem::MetricsBackend::MetricLevel>
level_for_metric_component_opt(
    Totem::MetricsBackend::MetricComponent metricComponent) {
    auto index = static_cast<size_t>(metricComponent);
    if (index >= metricCollectionLevels.size()) {
        return std::nullopt;
    }
    return metricCollectionLevels[index];
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
