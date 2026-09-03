#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include <cstdint>

namespace Totem::LedDisplay::detail {

struct Metrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using GaugeHandle = Totem::MetricsBackend::GaugeHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;
    using MetricUnit = Totem::MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc groupDef = {
        .name = "ledDisp",
        .level = Totem::MetricsBackend::MetricLevel::Baseline,
    };

    static constexpr MetricDesc queueFailuresDef = {
        .name = "qFail",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc badCommandsDef = {
        .name = "badCmd",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc renderFailuresDef = {
        .name = "rndFail",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc showFailuresDef = {
        .name = "showFail",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc inputFailuresDef = {
        .name = "inFail",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc commandsDef = {
        .name = "cmd",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc playsDef = {
        .name = "play",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc updatesDef = {
        .name = "update",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc updateMissesDef = {
        .name = "updMiss",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc stopsDef = {
        .name = "stop",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc fftInputsDef = {
        .name = "fftIn",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc wheelInputsDef = {
        .name = "whlIn",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc repeatedPresentsDef = {
        .name = "repeat",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc missedStrobesDef = {
        .name = "miss",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc overBudgetFramesDef = {
        .name = "slow",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc activeAnimationsDef = {
        .name = "active",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
        .gaugeDiscardIfUnder = 0,
    };
    static constexpr MetricDesc renderMaxUsDef = {
        .name = "rndMax",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };
    static constexpr MetricDesc showMaxUsDef = {
        .name = "showMax",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };
    static constexpr MetricDesc encodeMaxUsDef = {
        .name = "encMax",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };
    static constexpr MetricDesc spiQueueMaxUsDef = {
        .name = "spiQMax",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };
    static constexpr MetricDesc spiWaitMaxUsDef = {
        .name = "spiWMax",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };
    static constexpr MetricDesc frameMaxUsDef = {
        .name = "stepMax",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("LedDisplay", group);
        REGISTER_METRIC("LedDisplay", queueFailures, Counter, group);
        REGISTER_METRIC("LedDisplay", badCommands, Counter, group);
        REGISTER_METRIC("LedDisplay", renderFailures, Counter, group);
        REGISTER_METRIC("LedDisplay", showFailures, Counter, group);
        REGISTER_METRIC("LedDisplay", inputFailures, Counter, group);
        REGISTER_METRIC("LedDisplay", commands, Counter, group);
        REGISTER_METRIC("LedDisplay", plays, Counter, group);
        REGISTER_METRIC("LedDisplay", updates, Counter, group);
        REGISTER_METRIC("LedDisplay", updateMisses, Counter, group);
        REGISTER_METRIC("LedDisplay", stops, Counter, group);
        REGISTER_METRIC("LedDisplay", fftInputs, Counter, group);
        REGISTER_METRIC("LedDisplay", wheelInputs, Counter, group);
        REGISTER_METRIC("LedDisplay", repeatedPresents, Counter, group);
        REGISTER_METRIC("LedDisplay", missedStrobes, Counter, group);
        REGISTER_METRIC("LedDisplay", overBudgetFrames, Counter, group);
        REGISTER_METRIC("LedDisplay", activeAnimations, Gauge, group);
        REGISTER_METRIC("LedDisplay", renderMaxUs, Gauge, group);
        REGISTER_METRIC("LedDisplay", showMaxUs, Gauge, group);
        REGISTER_METRIC("LedDisplay", encodeMaxUs, Gauge, group);
        REGISTER_METRIC("LedDisplay", spiQueueMaxUs, Gauge, group);
        REGISTER_METRIC("LedDisplay", spiWaitMaxUs, Gauge, group);
        REGISTER_METRIC("LedDisplay", frameMaxUs, Gauge, group);

        return Metrics{
            .group = group,
            .queueFailures = queueFailures,
            .badCommands = badCommands,
            .renderFailures = renderFailures,
            .showFailures = showFailures,
            .inputFailures = inputFailures,
            .commands = commands,
            .plays = plays,
            .updates = updates,
            .updateMisses = updateMisses,
            .stops = stops,
            .fftInputs = fftInputs,
            .wheelInputs = wheelInputs,
            .repeatedPresents = repeatedPresents,
            .missedStrobes = missedStrobes,
            .overBudgetFrames = overBudgetFrames,
            .activeAnimations = activeAnimations,
            .renderMaxUs = renderMaxUs,
            .showMaxUs = showMaxUs,
            .encodeMaxUs = encodeMaxUs,
            .spiQueueMaxUs = spiQueueMaxUs,
            .spiWaitMaxUs = spiWaitMaxUs,
            .frameMaxUs = frameMaxUs,
        };
    }

    void addQueueFailure() const { METRIC_INCR(group, queueFailures, 1); }
    void addBadCommand() const { METRIC_INCR(group, badCommands, 1); }
    void addRenderFailure() const { METRIC_INCR(group, renderFailures, 1); }
    void addShowFailure() const { METRIC_INCR(group, showFailures, 1); }
    void addInputFailure() const { METRIC_INCR(group, inputFailures, 1); }
    void addCommand() const { METRIC_INCR(group, commands, 1); }
    void addPlay() const { METRIC_INCR(group, plays, 1); }
    void addUpdate() const { METRIC_INCR(group, updates, 1); }
    void addUpdateMiss() const { METRIC_INCR(group, updateMisses, 1); }
    void addStop() const { METRIC_INCR(group, stops, 1); }
    void addFftInput() const { METRIC_INCR(group, fftInputs, 1); }
    void addWheelInput() const { METRIC_INCR(group, wheelInputs, 1); }
    void addRepeatedPresent() const { METRIC_INCR(group, repeatedPresents, 1); }
    void addMissedStrobes(uint32_t count) const {
        METRIC_INCR(group, missedStrobes, count);
    }
    void addOverBudgetFrame() const { METRIC_INCR(group, overBudgetFrames, 1); }
    void setActiveAnimations(uint32_t count) const {
        METRIC_SET(group, activeAnimations, count);
    }

    void recordRenderDuration(uint32_t durationUs) {
        if (durationUs <= renderMaxUsValue) {
            return;
        }
        renderMaxUsValue = durationUs;
        METRIC_SET(group, renderMaxUs, durationUs);
    }

    void recordShowDuration(uint32_t durationUs) {
        if (durationUs <= showMaxUsValue) {
            return;
        }
        showMaxUsValue = durationUs;
        METRIC_SET(group, showMaxUs, durationUs);
    }

    void recordEncodeDuration(uint32_t durationUs) {
        if (durationUs <= encodeMaxUsValue) {
            return;
        }
        encodeMaxUsValue = durationUs;
        METRIC_SET(group, encodeMaxUs, durationUs);
    }

    void recordSpiQueueDuration(uint32_t durationUs) {
        if (durationUs <= spiQueueMaxUsValue) {
            return;
        }
        spiQueueMaxUsValue = durationUs;
        METRIC_SET(group, spiQueueMaxUs, durationUs);
    }

    void recordSpiWaitDuration(uint32_t durationUs) {
        if (durationUs <= spiWaitMaxUsValue) {
            return;
        }
        spiWaitMaxUsValue = durationUs;
        METRIC_SET(group, spiWaitMaxUs, durationUs);
    }

    void recordFrameDuration(uint32_t durationUs) {
        if (durationUs <= frameMaxUsValue) {
            return;
        }
        frameMaxUsValue = durationUs;
        METRIC_SET(group, frameMaxUs, durationUs);
    }

    GroupHandle group;
    CounterHandle queueFailures;
    CounterHandle badCommands;
    CounterHandle renderFailures;
    CounterHandle showFailures;
    CounterHandle inputFailures;
    CounterHandle commands;
    CounterHandle plays;
    CounterHandle updates;
    CounterHandle updateMisses;
    CounterHandle stops;
    CounterHandle fftInputs;
    CounterHandle wheelInputs;
    CounterHandle repeatedPresents;
    CounterHandle missedStrobes;
    CounterHandle overBudgetFrames;
    GaugeHandle activeAnimations;
    GaugeHandle renderMaxUs;
    GaugeHandle showMaxUs;
    GaugeHandle encodeMaxUs;
    GaugeHandle spiQueueMaxUs;
    GaugeHandle spiWaitMaxUs;
    GaugeHandle frameMaxUs;
    uint32_t renderMaxUsValue = 0;
    uint32_t showMaxUsValue = 0;
    uint32_t encodeMaxUsValue = 0;
    uint32_t spiQueueMaxUsValue = 0;
    uint32_t spiWaitMaxUsValue = 0;
    uint32_t frameMaxUsValue = 0;

    static constexpr auto component =
        MetricsBackend::MetricComponent::LedDisplay;
};

DEFINE_PREWARMED_METRICS_ACCESSORS(Metrics, metrics, prewarmMetrics)

} // namespace Totem::LedDisplay::detail
