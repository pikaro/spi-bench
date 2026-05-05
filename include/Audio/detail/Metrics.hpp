#pragma once

#include "Audio/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include <cmath>
#include <cstdint>

namespace Totem::Audio::detail {

struct Metrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using GaugeHandle = Totem::MetricsBackend::GaugeHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;
    using MetricUnit = Totem::MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc coreGroupDef = {
        .name = "audCore",
        .level = Totem::MetricsBackend::MetricLevel::Core,
    };
    static constexpr MetricGroupDesc groupDef = {
        .name = "audFft",
        .level = Totem::MetricsBackend::MetricLevel::Diagnostic,
    };
    static constexpr MetricGroupDesc profileGroupDef = {
        .name = "audProf",
        .level = Totem::MetricsBackend::MetricLevel::Profiling,
    };

    static constexpr MetricDesc stepsDef = {
        .name = "steps",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc copyDef = {
        .name = "copy",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc copyBytesDef = {
        .name = "copyB",
        .type = MetricType::Counter,
        .unit = MetricUnit::Bytes,
    };
    static constexpr MetricDesc emptyDef = {
        .name = "empty",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc skipsDef = {
        .name = "skip",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc probesDef = {
        .name = "probe",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc framesDef = {
        .name = "frame",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc dropsDef = {
        .name = "drop",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc beatsDef = {
        .name = "beat",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc copyUsDef = {
        .name = "copyUs",
        .type = MetricType::Counter,
        .unit = MetricUnit::Microseconds,
    };
    static constexpr MetricDesc probeUsDef = {
        .name = "probeUs",
        .type = MetricType::Counter,
        .unit = MetricUnit::Microseconds,
    };
    static constexpr MetricDesc frameUsDef = {
        .name = "frmUs",
        .type = MetricType::Counter,
        .unit = MetricUnit::Microseconds,
    };
    static constexpr MetricDesc bandUsDef = {
        .name = "bandUs",
        .type = MetricType::Counter,
        .unit = MetricUnit::Microseconds,
    };
    static constexpr MetricDesc cacheUsDef = {
        .name = "cacheUs",
        .type = MetricType::Counter,
        .unit = MetricUnit::Microseconds,
    };
    static constexpr MetricDesc dispatchUsDef = {
        .name = "dispUs",
        .type = MetricType::Counter,
        .unit = MetricUnit::Microseconds,
    };
    static constexpr MetricDesc beatUpdateUsDef = {
        .name = "btUpdUs",
        .type = MetricType::Counter,
        .unit = MetricUnit::Microseconds,
    };
    static constexpr MetricDesc beatDispatchUsDef = {
        .name = "btDisUs",
        .type = MetricType::Counter,
        .unit = MetricUnit::Microseconds,
    };
    static constexpr MetricDesc copyMaxDef = {
        .name = "copyMax",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };
    static constexpr MetricDesc probeMaxDef = {
        .name = "probeMax",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };
    static constexpr MetricDesc frameMaxDef = {
        .name = "frmMax",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };
    static constexpr MetricDesc bandMaxDef = {
        .name = "bandMax",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };
    static constexpr MetricDesc cacheMaxDef = {
        .name = "cacheMx",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };
    static constexpr MetricDesc dispatchMaxDef = {
        .name = "dispMax",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };
    static constexpr MetricDesc beatUpdateMaxDef = {
        .name = "btUpdMx",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };
    static constexpr MetricDesc beatDispatchMaxDef = {
        .name = "btDisMx",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };
    // Per-band magnitudes are intentionally omitted: recording gauges for each
    // band on every FFT frame would add atomics in the audio callback and
    // duplicate the already-published FftFrame payload.
    static constexpr MetricDesc beatEnergyDef = {
        .name = "beatE",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc bpmDef = {
        .name = "bpm",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("AudioCore", coreGroup);
        REGISTER_METRIC("AudioCore", drops, Counter, coreGroup);

        REGISTER_METRICS_GROUP("AudioFft", group);
        REGISTER_METRIC("AudioFft", steps, Counter, group);
        REGISTER_METRIC("AudioFft", copy, Counter, group);
        REGISTER_METRIC("AudioFft", copyBytes, Counter, group);
        REGISTER_METRIC("AudioFft", empty, Counter, group);
        REGISTER_METRIC("AudioFft", skips, Counter, group);
        REGISTER_METRIC("AudioFft", probes, Counter, group);
        REGISTER_METRIC("AudioFft", frames, Counter, group);
        REGISTER_METRIC("AudioFft", beats, Counter, group);
        REGISTER_METRIC("AudioFft", beatEnergy, Gauge, group);
        REGISTER_METRIC("AudioFft", bpm, Gauge, group);

        REGISTER_METRICS_GROUP("AudioProfiling", profileGroup);
        REGISTER_METRIC("AudioProfiling", copyUs, Counter, profileGroup);
        REGISTER_METRIC("AudioProfiling", probeUs, Counter, profileGroup);
        REGISTER_METRIC("AudioProfiling", frameUs, Counter, profileGroup);
        REGISTER_METRIC("AudioProfiling", bandUs, Counter, profileGroup);
        REGISTER_METRIC("AudioProfiling", cacheUs, Counter, profileGroup);
        REGISTER_METRIC("AudioProfiling", dispatchUs, Counter, profileGroup);
        REGISTER_METRIC("AudioProfiling", beatUpdateUs, Counter, profileGroup);
        REGISTER_METRIC("AudioProfiling", beatDispatchUs, Counter,
                        profileGroup);
        REGISTER_METRIC("AudioProfiling", copyMax, Gauge, profileGroup);
        REGISTER_METRIC("AudioProfiling", probeMax, Gauge, profileGroup);
        REGISTER_METRIC("AudioProfiling", frameMax, Gauge, profileGroup);
        REGISTER_METRIC("AudioProfiling", bandMax, Gauge, profileGroup);
        REGISTER_METRIC("AudioProfiling", cacheMax, Gauge, profileGroup);
        REGISTER_METRIC("AudioProfiling", dispatchMax, Gauge, profileGroup);
        REGISTER_METRIC("AudioProfiling", beatUpdateMax, Gauge, profileGroup);
        REGISTER_METRIC("AudioProfiling", beatDispatchMax, Gauge, profileGroup);

        return Metrics{
            .coreGroup = coreGroup,
            .drops = drops,
            .group = group,
            .steps = steps,
            .copy = copy,
            .copyBytes = copyBytes,
            .empty = empty,
            .skips = skips,
            .probes = probes,
            .frames = frames,
            .beats = beats,
            .beatEnergy = beatEnergy,
            .bpm = bpm,
            .profileGroup = profileGroup,
            .copyUs = copyUs,
            .probeUs = probeUs,
            .frameUs = frameUs,
            .bandUs = bandUs,
            .cacheUs = cacheUs,
            .dispatchUs = dispatchUs,
            .beatUpdateUs = beatUpdateUs,
            .beatDispatchUs = beatDispatchUs,
            .copyMax = copyMax,
            .probeMax = probeMax,
            .frameMax = frameMax,
            .bandMax = bandMax,
            .cacheMax = cacheMax,
            .dispatchMax = dispatchMax,
            .beatUpdateMax = beatUpdateMax,
            .beatDispatchMax = beatDispatchMax,
        };
    }

    void addTaskStep() { METRIC_INCR(group, steps, 1); }
    void addSourceUnavailableSkip() { METRIC_INCR(group, skips, 1); }
    void addDroppedFrame() { METRIC_INCR(coreGroup, drops, 1); }

    void addCopyResult(uint32_t bytes, uint32_t durationUs) {
        METRIC_INCR(group, copy, 1);
        METRIC_INCR(profileGroup, copyUs, durationUs);
        if (bytes == 0) {
            METRIC_INCR(group, empty, 1);
        } else {
            METRIC_INCR(group, copyBytes, bytes);
        }
        recordCopyDuration(durationUs);
    }

    void addReadinessProbe(uint32_t durationUs) {
        METRIC_INCR(group, probes, 1);
        METRIC_INCR(profileGroup, probeUs, durationUs);
        recordMax(probeMax, probeMaxValue, durationUs);
    }

    void addFrame(uint32_t durationUs) {
        METRIC_INCR(group, frames, 1);
        METRIC_INCR(profileGroup, frameUs, durationUs);
        recordMax(frameMax, frameMaxValue, durationUs);
    }

    void addBandCompute(uint32_t durationUs) {
        METRIC_INCR(profileGroup, bandUs, durationUs);
        recordMax(bandMax, bandMaxValue, durationUs);
    }

    void addMagnitudeCache(uint32_t durationUs) {
        METRIC_INCR(profileGroup, cacheUs, durationUs);
        recordMax(cacheMax, cacheMaxValue, durationUs);
    }

    void addFrameDispatch(uint32_t durationUs) {
        METRIC_INCR(profileGroup, dispatchUs, durationUs);
        recordMax(dispatchMax, dispatchMaxValue, durationUs);
    }

    void addBeatUpdate(uint32_t durationUs) {
        METRIC_INCR(profileGroup, beatUpdateUs, durationUs);
        recordMax(beatUpdateMax, beatUpdateMaxValue, durationUs);
    }

    void addBeatDispatch(uint32_t durationUs) {
        METRIC_INCR(profileGroup, beatDispatchUs, durationUs);
        recordMax(beatDispatchMax, beatDispatchMaxValue, durationUs);
    }

    void addBeat(const BeatResult &event, bool primary) {
        METRIC_INCR(group, beats, 1);
        if (!primary) {
            return;
        }
        METRIC_SET(group, beatEnergy, event.energy);
        METRIC_SET(group, bpm, static_cast<uint32_t>(std::lround(event.bpm)));
    }

    void recordCopyDuration(uint32_t durationUs) {
        recordMax(copyMax, copyMaxValue, durationUs);
    }

    void recordMax(GaugeHandle handle, uint32_t &current, uint32_t value) {
        if (value <= current) {
            return;
        }
        current = value;
        if constexpr (metrics_enabled(component, profileGroupDef)) {
            FAIL_IF_ERR_VOID(::MetricsService::recorder().set(handle, value),
                             "Failed to set audio FFT max metric");
        }
    }

    GroupHandle coreGroup;
    CounterHandle drops;
    GroupHandle group;
    CounterHandle steps;
    CounterHandle copy;
    CounterHandle copyBytes;
    CounterHandle empty;
    CounterHandle skips;
    CounterHandle probes;
    CounterHandle frames;
    CounterHandle beats;
    GaugeHandle beatEnergy;
    GaugeHandle bpm;
    GroupHandle profileGroup;
    CounterHandle copyUs;
    CounterHandle probeUs;
    CounterHandle frameUs;
    CounterHandle bandUs;
    CounterHandle cacheUs;
    CounterHandle dispatchUs;
    CounterHandle beatUpdateUs;
    CounterHandle beatDispatchUs;
    GaugeHandle copyMax;
    GaugeHandle probeMax;
    GaugeHandle frameMax;
    GaugeHandle bandMax;
    GaugeHandle cacheMax;
    GaugeHandle dispatchMax;
    GaugeHandle beatUpdateMax;
    GaugeHandle beatDispatchMax;
    uint32_t copyMaxValue = 0;
    uint32_t probeMaxValue = 0;
    uint32_t frameMaxValue = 0;
    uint32_t bandMaxValue = 0;
    uint32_t cacheMaxValue = 0;
    uint32_t dispatchMaxValue = 0;
    uint32_t beatUpdateMaxValue = 0;
    uint32_t beatDispatchMaxValue = 0;

    static constexpr auto component = MetricsBackend::MetricComponent::Audio;
    static constexpr bool profilingEnabled =
        metrics_enabled(component, profileGroupDef);
};

inline Metrics &metrics() {
    static Metrics instance = Metrics::create();
    return instance;
}

} // namespace Totem::Audio::detail
