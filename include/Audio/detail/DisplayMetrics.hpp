#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include "Types/Error.hpp"
#include <cstdint>

namespace Totem::Audio::detail {

struct DisplayMetrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using GaugeHandle = Totem::MetricsBackend::GaugeHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;
    using MetricUnit = Totem::MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc coreGroupDef = {
        .name = "dispCore",
        .level = Totem::MetricsBackend::MetricLevel::Core,
    };
    static constexpr MetricGroupDesc groupDef = {
        .name = "audDisp",
        .level = Totem::MetricsBackend::MetricLevel::Diagnostic,
    };
    static constexpr MetricGroupDesc profileGroupDef = {
        .name = "dispProf",
        .level = Totem::MetricsBackend::MetricLevel::Profiling,
    };

    static constexpr MetricDesc dropsDef = {
        .name = "drop",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc flushFailDef = {
        .name = "flFail",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc capturesDef = {
        .name = "capture",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc peaksDef = {
        .name = "peak",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc beatsDef = {
        .name = "beat",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc flushesDef = {
        .name = "flush",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc flushUsDef = {
        .name = "flushUs",
        .type = MetricType::Counter,
        .unit = MetricUnit::Microseconds,
    };
    static constexpr MetricDesc flushMaxDef = {
        .name = "flushMx",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };

    static DisplayMetrics create() {
        REGISTER_METRICS_GROUP("AudioDisplayCore", coreGroup);
        REGISTER_METRIC("AudioDisplayCore", drops, Counter, coreGroup);
        REGISTER_METRIC("AudioDisplayCore", flushFail, Counter, coreGroup);

        REGISTER_METRICS_GROUP("AudioDisplay", group);
        REGISTER_METRIC("AudioDisplay", captures, Counter, group);
        REGISTER_METRIC("AudioDisplay", peaks, Counter, group);
        REGISTER_METRIC("AudioDisplay", beats, Counter, group);
        REGISTER_METRIC("AudioDisplay", flushes, Counter, group);

        REGISTER_METRICS_GROUP("AudioDisplayProfiling", profileGroup);
        REGISTER_METRIC("AudioDisplayProfiling", flushUs, Counter,
                        profileGroup);
        REGISTER_METRIC("AudioDisplayProfiling", flushMax, Gauge,
                        profileGroup);

        return DisplayMetrics{
            .coreGroup = coreGroup,
            .drops = drops,
            .flushFail = flushFail,
            .group = group,
            .captures = captures,
            .peaks = peaks,
            .beats = beats,
            .flushes = flushes,
            .profileGroup = profileGroup,
            .flushUs = flushUs,
            .flushMax = flushMax,
        };
    }

    void addCapturedFrame() const { METRIC_INCR(group, captures, 1); }
    void addDroppedFrame() const { METRIC_INCR(coreGroup, drops, 1); }
    void addPeak() const { METRIC_INCR(group, peaks, 1); }
    void addBeat() const { METRIC_INCR(group, beats, 1); }

    void addFlush(ReturnCode result, uint32_t durationUs) {
        if (!result.ok()) {
            METRIC_INCR(coreGroup, flushFail, 1);
            return;
        }
        METRIC_INCR(group, flushes, 1);
        METRIC_INCR(profileGroup, flushUs, durationUs);
        if (durationUs <= flushMaxValue) {
            return;
        }
        flushMaxValue = durationUs;
        METRIC_SET(profileGroup, flushMax, durationUs);
    }

    GroupHandle coreGroup;
    CounterHandle drops;
    CounterHandle flushFail;
    GroupHandle group;
    CounterHandle captures;
    CounterHandle peaks;
    CounterHandle beats;
    CounterHandle flushes;
    GroupHandle profileGroup;
    CounterHandle flushUs;
    GaugeHandle flushMax;
    uint32_t flushMaxValue = 0;

    static constexpr auto component = MetricsBackend::MetricComponent::Audio;
    static constexpr bool profilingEnabled =
        metrics_enabled(component, profileGroupDef);
};

DEFINE_PREWARMED_METRICS_ACCESSORS(DisplayMetrics, displayMetrics,
                                   prewarmDisplayMetrics)

} // namespace Totem::Audio::detail
