// IWYU pragma: private

#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include "Wire/I2C/Interfaces/Ina2xxConfig.hpp"
#include <cstdint>

namespace Totem::Wire::I2C::detail {

struct Ina2xxMetrics {
    using GroupHandle = MetricsBackend::GroupHandle;
    using CounterHandle = MetricsBackend::CounterHandle;
    using GaugeHandle = MetricsBackend::GaugeHandle;
    using SignedGaugeHandle = MetricsBackend::SignedGaugeHandle;
    using MetricGroupDesc = MetricsBackend::MetricGroupDesc;
    using MetricDesc = MetricsBackend::MetricDesc;
    using MetricType = MetricsBackend::MetricType;
    using MetricUnit = MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc baseDef{
        .name = "ina2xx",
        .level = MetricsBackend::MetricLevel::Baseline,
    };
    static constexpr MetricDesc shuntUvDef{.name = "shuntUv",
                                           .type = MetricType::SignedGauge};
    static constexpr MetricDesc busMvDef{
        .name = "busMv",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Millivolts,
    };
    static constexpr MetricDesc currentUaDef{.name = "currUa",
                                             .type = MetricType::SignedGauge};
    static constexpr MetricDesc powerMwDef{.name = "powerMw",
                                           .type = MetricType::SignedGauge};
    static constexpr MetricDesc temperatureMcDef{
        .name = "tempMc", .type = MetricType::SignedGauge};
    static constexpr MetricDesc energyMjDef{.name = "energyMj",
                                            .type = MetricType::Gauge};
    static constexpr MetricDesc chargeMcDef{.name = "chargeMc",
                                            .type = MetricType::SignedGauge};
    static constexpr MetricDesc capabilitiesDef{.name = "cap",
                                                .type = MetricType::Gauge};
    static constexpr MetricDesc validDef{.name = "valid",
                                         .type = MetricType::Gauge};
    static constexpr MetricDesc busStateDef{.name = "busState",
                                            .type = MetricType::Gauge};
    static constexpr MetricDesc currentStateDef{.name = "curState",
                                                .type = MetricType::Gauge};
    static constexpr MetricDesc ageMsDef{
        .name = "ageMs",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Milliseconds,
    };
    static constexpr MetricDesc samplesDef{.name = "samples",
                                           .type = MetricType::Counter};
    static constexpr MetricDesc failuresDef{.name = "fail",
                                            .type = MetricType::Counter};
    static constexpr MetricDesc overflowsDef{.name = "ovf",
                                             .type = MetricType::Counter};
    static constexpr MetricDesc alertsDef{.name = "alerts",
                                          .type = MetricType::Counter};

    static Ina2xxMetrics create(const MetricGroupDesc &groupDef) {
        REGISTER_METRICS_GROUP("Ina2xx", group);
        REGISTER_METRIC("Ina2xx", shuntUv, SignedGauge, group);
        REGISTER_METRIC("Ina2xx", busMv, Gauge, group);
        REGISTER_METRIC("Ina2xx", currentUa, SignedGauge, group);
        REGISTER_METRIC("Ina2xx", powerMw, SignedGauge, group);
        REGISTER_METRIC("Ina2xx", temperatureMc, SignedGauge, group);
        REGISTER_METRIC("Ina2xx", energyMj, Gauge, group);
        REGISTER_METRIC("Ina2xx", chargeMc, SignedGauge, group);
        REGISTER_METRIC("Ina2xx", capabilities, Gauge, group);
        REGISTER_METRIC("Ina2xx", valid, Gauge, group);
        REGISTER_METRIC("Ina2xx", busState, Gauge, group);
        REGISTER_METRIC("Ina2xx", currentState, Gauge, group);
        REGISTER_METRIC("Ina2xx", ageMs, Gauge, group);
        REGISTER_METRIC("Ina2xx", samples, Counter, group);
        REGISTER_METRIC("Ina2xx", failures, Counter, group);
        REGISTER_METRIC("Ina2xx", overflows, Counter, group);
        REGISTER_METRIC("Ina2xx", alerts, Counter, group);
        return {
            .group = group,
            .shuntUv = shuntUv,
            .busMv = busMv,
            .currentUa = currentUa,
            .powerMw = powerMw,
            .temperatureMc = temperatureMc,
            .energyMj = energyMj,
            .chargeMc = chargeMc,
            .capabilities = capabilities,
            .valid = valid,
            .busState = busState,
            .currentState = currentState,
            .ageMs = ageMs,
            .samples = samples,
            .failures = failures,
            .overflows = overflows,
            .alerts = alerts,
        };
    }

    void initialize(uint32_t capabilityMask) const {
        METRIC_SET(base, shuntUv, 0);
        METRIC_SET(base, busMv, 0);
        METRIC_SET(base, currentUa, 0);
        METRIC_SET(base, powerMw, 0);
        METRIC_SET(base, temperatureMc, 0);
        METRIC_SET(base, energyMj, 0);
        METRIC_SET(base, chargeMc, 0);
        METRIC_SET(base, capabilities, capabilityMask);
        METRIC_SET(base, valid, 0);
        METRIC_SET(base, busState,
                   static_cast<uint32_t>(Ina2xxLimitState::Normal));
        METRIC_SET(base, currentState,
                   static_cast<uint32_t>(Ina2xxLimitState::Normal));
        METRIC_SET(base, ageMs, UINT32_MAX);
    }

    void recordSample(const Ina2xxSample &sample,
                      Ina2xxLimitState busLimitState,
                      Ina2xxLimitState currentLimitState) const {
        METRIC_SET(base, shuntUv, sample.shuntMicrovolts);
        METRIC_SET(base, busMv, sample.busMillivolts);
        METRIC_SET(base, currentUa, sample.currentMicroamps);
        METRIC_SET(base, powerMw, sample.powerMilliwatts);
        METRIC_SET(base, temperatureMc, sample.temperatureMillicelsius);
        METRIC_SET(base, energyMj, sample.energyMillijoules);
        METRIC_SET(base, chargeMc, sample.chargeMillicoulombs);
        METRIC_SET(base, valid, sample.validCapabilities);
        METRIC_SET(base, busState, static_cast<uint32_t>(busLimitState));
        METRIC_SET(base, currentState,
                   static_cast<uint32_t>(currentLimitState));
        METRIC_SET(base, ageMs, 0);
        METRIC_INCR(base, samples, 1);
    }

    void recordAge(uint32_t sampleAgeMs) const {
        METRIC_SET(base, ageMs, sampleAgeMs);
    }
    void addFailure() const { METRIC_INCR(base, failures, 1); }
    void addOverflow() const { METRIC_INCR(base, overflows, 1); }
    void addAlert() const { METRIC_INCR(base, alerts, 1); }

    GroupHandle group;
    SignedGaugeHandle shuntUv;
    GaugeHandle busMv;
    SignedGaugeHandle currentUa;
    SignedGaugeHandle powerMw;
    SignedGaugeHandle temperatureMc;
    GaugeHandle energyMj;
    SignedGaugeHandle chargeMc;
    GaugeHandle capabilities;
    GaugeHandle valid;
    GaugeHandle busState;
    GaugeHandle currentState;
    GaugeHandle ageMs;
    CounterHandle samples;
    CounterHandle failures;
    CounterHandle overflows;
    CounterHandle alerts;

    static constexpr auto component = MetricsBackend::MetricComponent::I2C;
};

} // namespace Totem::Wire::I2C::detail
