#pragma once

#include "AudioAfe/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Totem::AudioAfe::detail {

struct Metrics {
    using GroupHandle = MetricsBackend::GroupHandle;
    using CounterHandle = MetricsBackend::CounterHandle;
    using GaugeHandle = MetricsBackend::GaugeHandle;
    using SignedGaugeHandle = MetricsBackend::SignedGaugeHandle;
    using MetricGroupDesc = MetricsBackend::MetricGroupDesc;
    using MetricDesc = MetricsBackend::MetricDesc;
    using MetricType = MetricsBackend::MetricType;
    using MetricUnit = MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc groupDef{
        .name = "audAfe",
        .level = MetricsBackend::MetricLevel::Baseline,
    };
    static constexpr MetricDesc sourceBytesDef{
        .name = "srcB", .type = MetricType::Counter, .unit = MetricUnit::Bytes};
    static constexpr MetricDesc emptyReadsDef{.name = "empty",
                                              .type = MetricType::Counter};
    static constexpr MetricDesc shortReadsDef{.name = "short",
                                              .type = MetricType::Counter};
    static constexpr MetricDesc feedFramesDef{.name = "feed",
                                              .type = MetricType::Counter};
    static constexpr MetricDesc fetchFramesDef{.name = "fetch",
                                               .type = MetricType::Counter};
    static constexpr MetricDesc fetchMissesDef{.name = "noFetch",
                                               .type = MetricType::Counter};
    static constexpr MetricDesc failuresDef{.name = "fail",
                                            .type = MetricType::Counter};
    static constexpr MetricDesc wakesDef{.name = "wake",
                                         .type = MetricType::Counter};
    static constexpr MetricDesc vadSpeechDef{.name = "vadSp",
                                             .type = MetricType::Counter};
    static constexpr MetricDesc vadSilenceDef{.name = "vadSil",
                                              .type = MetricType::Counter};
    static constexpr MetricDesc ringFreeDef{.name = "ringPct",
                                            .type = MetricType::Gauge,
                                            .unit = MetricUnit::Percent};
    static constexpr MetricDesc inputVolumeDbDef{
        .name = "inVolDb",
        .type = MetricType::SignedGauge,
        .unit = MetricUnit::Decibels,
    };
    static constexpr MetricDesc peakDef{.name = "peak",
                                        .type = MetricType::Gauge};
    static constexpr MetricDesc rmsDef{.name = "rms",
                                       .type = MetricType::Gauge};
    static constexpr MetricDesc clippedDef{.name = "clip",
                                           .type = MetricType::Counter};

    static Metrics create() {
        REGISTER_METRICS_GROUP("AudioAfe", group);
        REGISTER_METRIC("AudioAfe", sourceBytes, Counter, group);
        REGISTER_METRIC("AudioAfe", emptyReads, Counter, group);
        REGISTER_METRIC("AudioAfe", shortReads, Counter, group);
        REGISTER_METRIC("AudioAfe", feedFrames, Counter, group);
        REGISTER_METRIC("AudioAfe", fetchFrames, Counter, group);
        REGISTER_METRIC("AudioAfe", fetchMisses, Counter, group);
        REGISTER_METRIC("AudioAfe", failures, Counter, group);
        REGISTER_METRIC("AudioAfe", wakes, Counter, group);
        REGISTER_METRIC("AudioAfe", vadSpeech, Counter, group);
        REGISTER_METRIC("AudioAfe", vadSilence, Counter, group);
        REGISTER_METRIC("AudioAfe", ringFree, Gauge, group);
        REGISTER_METRIC("AudioAfe", inputVolumeDb, SignedGauge, group);
        REGISTER_METRIC("AudioAfe", peak, Gauge, group);
        REGISTER_METRIC("AudioAfe", rms, Gauge, group);
        REGISTER_METRIC("AudioAfe", clipped, Counter, group);
        return Metrics{.group = group,
                       .sourceBytes = sourceBytes,
                       .emptyReads = emptyReads,
                       .shortReads = shortReads,
                       .feedFrames = feedFrames,
                       .fetchFrames = fetchFrames,
                       .fetchMisses = fetchMisses,
                       .failures = failures,
                       .wakes = wakes,
                       .vadSpeech = vadSpeech,
                       .vadSilence = vadSilence,
                       .ringFree = ringFree,
                       .inputVolumeDb = inputVolumeDb,
                       .peak = peak,
                       .rms = rms,
                       .clipped = clipped};
    }

    void recordSourceRead(std::size_t requested, std::size_t bytes) const {
        if (bytes == 0) {
            METRIC_INCR(group, emptyReads, 1);
            return;
        }
        METRIC_INCR(group, sourceBytes, static_cast<uint32_t>(bytes));
        if (bytes < requested) {
            METRIC_INCR(group, shortReads, 1);
        }
    }
    void addFeed() const { METRIC_INCR(group, feedFrames, 1); }
    void addFetch() const { METRIC_INCR(group, fetchFrames, 1); }
    void addFetchMiss() const { METRIC_INCR(group, fetchMisses, 1); }
    void addFailure() const { METRIC_INCR(group, failures, 1); }
    void addWake() const { METRIC_INCR(group, wakes, 1); }
    void addVad(VadState state) const {
        if (state == VadState::Speech) {
            METRIC_INCR(group, vadSpeech, 1);
        } else {
            METRIC_INCR(group, vadSilence, 1);
        }
    }
    void recordAfe(float volumeDb, float ringFreeFraction) const {
        const auto roundedVolumeDb =
            static_cast<int32_t>(std::lround(static_cast<double>(volumeDb)));
        const auto normalized = ringFreeFraction <= 1.0F
                                    ? ringFreeFraction * 100.0F
                                    : ringFreeFraction;
        const auto percent = static_cast<uint32_t>(
            std::lround(std::clamp(normalized, 0.0F, 100.0F)));
        METRIC_SET(group, inputVolumeDb, roundedVolumeDb);
        METRIC_SET(group, ringFree, percent);
    }
    void recordSignal(std::span<const int16_t> samples) const {
        if (samples.empty()) {
            return;
        }
        uint32_t peakValue = 0;
        uint64_t squareSum = 0;
        uint32_t clippedCount = 0;
        for (const auto sample : samples) {
            const auto wide = static_cast<int32_t>(sample);
            const auto magnitude = static_cast<uint32_t>(
                wide < 0 ? -static_cast<int64_t>(wide) : wide);
            peakValue = std::max(peakValue, magnitude);
            squareSum += static_cast<uint64_t>(wide * wide);
            if (sample == INT16_MIN || sample == INT16_MAX) {
                ++clippedCount;
            }
        }
        const auto mean = static_cast<double>(squareSum) /
                          static_cast<double>(samples.size());
        METRIC_SET(group, peak, peakValue);
        METRIC_SET(group, rms,
                   static_cast<uint32_t>(std::lround(std::sqrt(mean))));
        if (clippedCount > 0) {
            METRIC_INCR(group, clipped, clippedCount);
        }
    }

    GroupHandle group;
    CounterHandle sourceBytes;
    CounterHandle emptyReads;
    CounterHandle shortReads;
    CounterHandle feedFrames;
    CounterHandle fetchFrames;
    CounterHandle fetchMisses;
    CounterHandle failures;
    CounterHandle wakes;
    CounterHandle vadSpeech;
    CounterHandle vadSilence;
    GaugeHandle ringFree;
    SignedGaugeHandle inputVolumeDb;
    GaugeHandle peak;
    GaugeHandle rms;
    CounterHandle clipped;

    static constexpr auto component = MetricsBackend::MetricComponent::Audio;
};

DEFINE_PREWARMED_METRICS_ACCESSORS(Metrics, metrics, prewarmMetrics)

} // namespace Totem::AudioAfe::detail
