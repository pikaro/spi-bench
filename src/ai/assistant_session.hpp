#pragma once

#include "AudioAfe/Interfaces/Types.hpp"
#include "AudioSink/Facade.hpp"
#include "AudioTools/CoreAudio/BaseConverter.h"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include "Services/StatusLed.hpp"
#include "StatusLed/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include "Wifi/Facade.hpp"
#include "assistant_websocket.hpp"
#include "esp_heap_caps.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "pcm16_playback_resampler.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <limits>
#include <span>
#include <string_view>
#include <sys/time.h>

namespace AiAudio {

enum class AssistantTurnState : uint8_t {
    WaitingForWake,
    Recording,
    AwaitingResponse,
    PlayingResponse,
};

enum class AssistantInformationalStatus : uint8_t {
    CoreReady,
    Off,
    Listening,
    Playback,
};

enum class CaptureEndReason : uint8_t {
    None,
    VadSilence,
    NoSpeechTimeout,
    MaximumDuration,
    CaptureOverflow,
    NetworkUnavailable,
    ClockUnavailable,
    TransportFailure,
    PipelineStop,
};

enum class TurnAbortReason : uint8_t {
    None,
    NoSpeech,
    CaptureOverflow,
    NetworkUnavailable,
    ClockUnavailable,
    Transport,
    Protocol,
    ResponseTimeout,
    Playback,
    PipelineStop,
};

struct AssistantWakeProfile {
    uint8_t wakeNetModelIndex = 0;
    uint8_t wakeWordIndex = 0;
    std::string_view wakeWord{};
    std::string_view voice{};

    [[nodiscard]] constexpr bool validate() const {
        return wakeNetModelIndex >= 1 && wakeNetModelIndex <= 2 &&
               wakeWordIndex >= 1 && !wakeWord.empty() &&
               validAssistantVoice(voice);
    }
};

struct AssistantSessionConfig {
    AssistantWebSocketConfig webSocket{};
    std::span<const AssistantWakeProfile> wakeProfiles{};
    Totem::AudioSink::AudioInfo inputAudio{
        .sampleRate = 16000,
        .channels = 1,
        .bitsPerSample = 16,
    };
    Totem::AudioSink::AudioInfo responseAudio{
        .sampleRate = Pcm16PlaybackResampler::inputSampleRate,
        .channels = 1,
        .bitsPerSample = 16,
    };
    Totem::AudioSink::AudioInfo playbackAudio{
        .sampleRate = Pcm16PlaybackResampler::outputSampleRate,
        .channels = 1,
        .bitsPerSample = 16,
    };
    uint32_t noSpeechTimeoutMs = 5000;
    uint32_t maximumSessionMs = 30000;
    std::size_t captureCapacityBytes = 128000;
    std::size_t playbackWriteBytes = 4096;
    std::size_t playbackQueueCapacityBytes = 96U * 1024U;
    std::size_t playbackPrebufferBytes = 32U * 1024U;
    std::size_t playbackPrimeWriteBytes = 512;
    uint32_t playbackQueueWriteTimeoutMs = 1000;
    uint32_t playbackQueuePollMs = 10;
    uint32_t playbackCompletionTimeoutMs = 30000;
    uint32_t playbackDrainMarginMs = 10;
    uint32_t uploadPollMs = 10;
    uint32_t responseTimeoutMs = 120000;
    uint32_t playbackWriteTimeoutMs = 3000;
    uint32_t writerQuiesceTimeoutMs = 100;
    bool playbackStartAtZeroCrossing = true;
    float responseGain = 1.0F;
    const char *sntpServer = "pool.ntp.org";
    std::time_t minimumValidEpoch = 1735689600; // 2025-01-01 UTC
    uint32_t sntpSyncTimeoutMs = 30000;
    uint32_t readinessPollMs = 250;
    uint32_t readinessLogIntervalMs = 10000;
    const char *taskName = "AiAssistant";
    uint32_t taskStackBytes = 12U * 1024U;
    UBaseType_t taskPriority = 4;
    BaseType_t taskCore = 0;
    const char *playbackTaskName = "AiPlayback";
    uint32_t playbackTaskStackBytes = 4096;
    UBaseType_t playbackTaskPriority = 5;
    BaseType_t playbackTaskCore = 0;
    Totem::StatusLed::StateDef listeningStatus{
        .name = "Listening",
        .color = {.red = 24, .green = 24, .blue = 24},
        .kind = Totem::StatusLed::StateKind::Informational,
    };
    Totem::StatusLed::StateDef recordingStatus{
        .name = "Recording",
        .color = {.red = 160, .green = 80, .blue = 0},
        .kind = Totem::StatusLed::StateKind::Warning,
    };
    Totem::StatusLed::StateDef playbackStatus{
        .name = "Playback",
        .color = {.red = 96, .green = 0, .blue = 128},
        .kind = Totem::StatusLed::StateKind::Informational,
    };

    [[nodiscard]] bool validateWakeProfiles() const {
        if (wakeProfiles.empty()) {
            return false;
        }
        for (std::size_t index = 0; index < wakeProfiles.size(); ++index) {
            const auto &profile = wakeProfiles[index];
            if (!profile.validate()) {
                return false;
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                const auto &candidate = wakeProfiles[previous];
                if (candidate.wakeNetModelIndex ==
                        profile.wakeNetModelIndex &&
                    candidate.wakeWordIndex == profile.wakeWordIndex) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] bool validate() const {
        const auto maximumCapture =
            static_cast<std::size_t>(std::numeric_limits<uint32_t>::max());
        return webSocket.validate() && validateWakeProfiles() &&
               inputAudio.validate() &&
               responseAudio.validate() && playbackAudio.validate() &&
               inputAudio.sampleRate == 16000 && inputAudio.channels == 1 &&
               inputAudio.bitsPerSample == 16 && noSpeechTimeoutMs > 0 &&
               responseAudio.sampleRate ==
                   Pcm16PlaybackResampler::inputSampleRate &&
               responseAudio.channels == 1 &&
               responseAudio.bitsPerSample == 16 &&
               playbackAudio.sampleRate ==
                   Pcm16PlaybackResampler::outputSampleRate &&
               playbackAudio.channels == 1 &&
               playbackAudio.bitsPerSample == 16 &&
               noSpeechTimeoutMs < maximumSessionMs &&
               captureCapacityBytes >= webSocket.appendPcmBytes * 2U &&
               captureCapacityBytes <= maximumCapture &&
               captureCapacityBytes % sizeof(int16_t) == 0 &&
               playbackWriteBytes > 0 &&
               playbackWriteBytes % sizeof(int16_t) == 0 &&
               playbackQueueCapacityBytes >= playbackWriteBytes * 2U &&
               playbackQueueCapacityBytes <= maximumCapture &&
               playbackQueueCapacityBytes % sizeof(int16_t) == 0 &&
               playbackPrebufferBytes >= playbackWriteBytes &&
               playbackPrebufferBytes < playbackQueueCapacityBytes &&
               playbackPrebufferBytes % sizeof(int16_t) == 0 &&
               playbackPrimeWriteBytes > 0 &&
               playbackPrimeWriteBytes <= playbackWriteBytes &&
               playbackPrimeWriteBytes % sizeof(int16_t) == 0 &&
               playbackQueueWriteTimeoutMs > 0 && playbackQueuePollMs > 0 &&
               playbackQueuePollMs <= playbackQueueWriteTimeoutMs &&
               playbackCompletionTimeoutMs > playbackWriteTimeoutMs &&
               playbackDrainMarginMs <= 1000 && uploadPollMs > 0 &&
               responseTimeoutMs > webSocket.readTimeoutMs &&
               playbackWriteTimeoutMs > 0 && writerQuiesceTimeoutMs > 0 &&
               responseGain > 0.0F && responseGain <= 4.0F &&
               sntpServer != nullptr && sntpServer[0] != '\0' &&
               minimumValidEpoch > 0 && sntpSyncTimeoutMs > 0 &&
               readinessPollMs > 0 &&
               readinessLogIntervalMs >= readinessPollMs &&
               taskName != nullptr && taskName[0] != '\0' &&
               taskStackBytes >= 4096 && taskPriority > 0 &&
               (taskCore == 0 || taskCore == 1) &&
               playbackTaskName != nullptr && playbackTaskName[0] != '\0' &&
               playbackTaskStackBytes >= 4096 && playbackTaskPriority > 0 &&
               (playbackTaskCore == 0 || playbackTaskCore == 1) &&
               listeningStatus.validate() && recordingStatus.validate() &&
               playbackStatus.validate() &&
               listeningStatus.color != recordingStatus.color &&
               listeningStatus.color != playbackStatus.color &&
               recordingStatus.color != playbackStatus.color;
    }
};

namespace detail {

struct AssistantMetrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using GaugeHandle = Totem::MetricsBackend::GaugeHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;
    using MetricUnit = Totem::MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc groupDef{
        .name = "aiAsst",
        .level = Totem::MetricsBackend::MetricLevel::Baseline,
    };
    static constexpr MetricGroupDesc outcomeGroupDef{
        .name = "aiAsOut",
        .level = Totem::MetricsBackend::MetricLevel::Baseline,
    };
    static constexpr MetricDesc startedDef{.name = "start",
                                           .type = MetricType::Counter};
    static constexpr MetricDesc committedDef{.name = "commit",
                                             .type = MetricType::Counter};
    static constexpr MetricDesc completedDef{.name = "done",
                                             .type = MetricType::Counter};
    static constexpr MetricDesc abortedDef{.name = "abort",
                                           .type = MetricType::Counter};
    static constexpr MetricDesc busyWakeDef{.name = "busyWake",
                                            .type = MetricType::Counter};
    static constexpr MetricDesc vadEndDef{.name = "endVad",
                                          .type = MetricType::Counter};
    static constexpr MetricDesc noSpeechEndDef{.name = "endNoSp",
                                               .type = MetricType::Counter};
    static constexpr MetricDesc maximumEndDef{.name = "endMax",
                                              .type = MetricType::Counter};
    static constexpr MetricDesc connectDef{.name = "connect",
                                           .type = MetricType::Counter};
    static constexpr MetricDesc connectFailureDef{.name = "connFail",
                                                  .type = MetricType::Counter};
    static constexpr MetricDesc capturedBytesDef{
        .name = "captureB",
        .type = MetricType::Counter,
        .unit = MetricUnit::Bytes,
    };
    static constexpr MetricDesc sentBytesDef{
        .name = "sentB",
        .type = MetricType::Counter,
        .unit = MetricUnit::Bytes,
    };
    static constexpr MetricDesc playedBytesDef{
        .name = "playedB",
        .type = MetricType::Counter,
        .unit = MetricUnit::Bytes,
    };
    static constexpr MetricDesc overflowDef{.name = "overflow",
                                            .type = MetricType::Counter};
    static constexpr MetricDesc unknownEventDef{.name = "unknown",
                                                .type = MetricType::Counter};
    static constexpr MetricDesc statusFailureDef{.name = "statFail",
                                                 .type = MetricType::Counter};
    static constexpr MetricDesc shortWriteDef{.name = "shortWr",
                                              .type = MetricType::Counter};
    static constexpr MetricDesc playbackUnderflowDef{
        .name = "pbUnder", .type = MetricType::Counter, .logIfZero = true};
    static constexpr MetricDesc playbackOverflowDef{
        .name = "pbOver", .type = MetricType::Counter, .logIfZero = true};
    static constexpr MetricDesc emptySampleDef{.name = "empty",
                                              .type = MetricType::Counter};
    static constexpr MetricDesc stateDef{
        .name = "state", .type = MetricType::Gauge, .logIfZero = true};
    static constexpr MetricDesc captureHighWaterDef{
        .name = "capHigh",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Bytes,
    };
    static constexpr MetricDesc connectDurationDef{
        .name = "connMs",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Milliseconds,
    };
    static constexpr MetricDesc firstAudioDurationDef{
        .name = "firstMs",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Milliseconds,
    };
    static constexpr MetricDesc playbackPrimeDurationDef{
        .name = "primeMs",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Milliseconds,
        .logIfZero = true,
    };
    static constexpr MetricDesc turnDurationDef{
        .name = "turnMs",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Milliseconds,
    };
    static constexpr MetricDesc playbackQueueDepthDef{
        .name = "pbDepth",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Bytes,
        .logIfZero = true,
    };
    static constexpr MetricDesc playbackQueueHighWaterDef{
        .name = "pbHigh",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Bytes,
    };
    static constexpr MetricDesc playbackWaitDurationDef{
        .name = "pbWaitMs",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Milliseconds,
        .logIfZero = true,
    };
    static constexpr MetricDesc playbackWriteDurationDef{
        .name = "pbWrMs",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Milliseconds,
        .logIfZero = true,
    };
    static constexpr MetricDesc playbackBoundaryInputDef{
        .name = "pbEdgeIn",
        .type = MetricType::Gauge,
        .logIfZero = true,
    };
    static constexpr MetricDesc playbackBoundaryOutputDef{
        .name = "pbEdgeOut",
        .type = MetricType::Gauge,
        .logIfZero = true,
    };
    static constexpr MetricDesc playbackBoundaryMutedDef{
        .name = "pbMute",
        .type = MetricType::Gauge,
        .logIfZero = true,
    };

    static AssistantMetrics create() {
        REGISTER_METRICS_GROUP("AssistantSession", group);
        REGISTER_METRICS_GROUP("AssistantSession", outcomeGroup);
        REGISTER_METRIC("AssistantSession", started, Counter, group);
        REGISTER_METRIC("AssistantSession", committed, Counter, group);
        REGISTER_METRIC("AssistantSession", completed, Counter, group);
        REGISTER_METRIC("AssistantSession", aborted, Counter, group);
        REGISTER_METRIC("AssistantSession", busyWake, Counter, group);
        REGISTER_METRIC("AssistantSession", vadEnd, Counter, group);
        REGISTER_METRIC("AssistantSession", noSpeechEnd, Counter, group);
        REGISTER_METRIC("AssistantSession", maximumEnd, Counter, group);
        REGISTER_METRIC("AssistantSession", connect, Counter, group);
        REGISTER_METRIC("AssistantSession", connectFailure, Counter, group);
        REGISTER_METRIC("AssistantSession", capturedBytes, Counter, group);
        REGISTER_METRIC("AssistantSession", sentBytes, Counter, group);
        REGISTER_METRIC("AssistantSession", playedBytes, Counter, group);
        REGISTER_METRIC("AssistantSession", overflow, Counter, group);
        REGISTER_METRIC("AssistantSession", unknownEvent, Counter, group);
        REGISTER_METRIC("AssistantSession", statusFailure, Counter, group);
        REGISTER_METRIC("AssistantSession", shortWrite, Counter, group);
        REGISTER_METRIC("AssistantSession", playbackUnderflow, Counter, group);
        REGISTER_METRIC("AssistantSession", playbackOverflow, Counter, group);
        REGISTER_METRIC("AssistantSession", emptySample, Counter, outcomeGroup);
        REGISTER_METRIC("AssistantSession", state, Gauge, group);
        REGISTER_METRIC("AssistantSession", captureHighWater, Gauge, group);
        REGISTER_METRIC("AssistantSession", connectDuration, Gauge, group);
        REGISTER_METRIC("AssistantSession", firstAudioDuration, Gauge, group);
        REGISTER_METRIC("AssistantSession", playbackPrimeDuration, Gauge,
                        group);
        REGISTER_METRIC("AssistantSession", turnDuration, Gauge, group);
        REGISTER_METRIC("AssistantSession", playbackQueueDepth, Gauge, group);
        REGISTER_METRIC("AssistantSession", playbackQueueHighWater, Gauge,
                        group);
        REGISTER_METRIC("AssistantSession", playbackWaitDuration, Gauge, group);
        REGISTER_METRIC("AssistantSession", playbackWriteDuration, Gauge,
                        group);
        REGISTER_METRIC("AssistantSession", playbackBoundaryInput, Gauge,
                        group);
        REGISTER_METRIC("AssistantSession", playbackBoundaryOutput, Gauge,
                        group);
        REGISTER_METRIC("AssistantSession", playbackBoundaryMuted, Gauge,
                        group);
        return {
            .group = group,
            .outcomeGroup = outcomeGroup,
            .started = started,
            .committed = committed,
            .completed = completed,
            .aborted = aborted,
            .busyWake = busyWake,
            .vadEnd = vadEnd,
            .noSpeechEnd = noSpeechEnd,
            .maximumEnd = maximumEnd,
            .connect = connect,
            .connectFailure = connectFailure,
            .capturedBytes = capturedBytes,
            .sentBytes = sentBytes,
            .playedBytes = playedBytes,
            .overflow = overflow,
            .unknownEvent = unknownEvent,
            .statusFailure = statusFailure,
            .shortWrite = shortWrite,
            .playbackUnderflow = playbackUnderflow,
            .playbackOverflow = playbackOverflow,
            .emptySample = emptySample,
            .state = state,
            .captureHighWater = captureHighWater,
            .connectDuration = connectDuration,
            .firstAudioDuration = firstAudioDuration,
            .playbackPrimeDuration = playbackPrimeDuration,
            .turnDuration = turnDuration,
            .playbackQueueDepth = playbackQueueDepth,
            .playbackQueueHighWater = playbackQueueHighWater,
            .playbackWaitDuration = playbackWaitDuration,
            .playbackWriteDuration = playbackWriteDuration,
            .playbackBoundaryInput = playbackBoundaryInput,
            .playbackBoundaryOutput = playbackBoundaryOutput,
            .playbackBoundaryMuted = playbackBoundaryMuted,
        };
    }

    void addStarted() const { METRIC_INCR(group, started, 1); }
    void addCommitted() const { METRIC_INCR(group, committed, 1); }
    void addCompleted() const { METRIC_INCR(group, completed, 1); }
    void addAborted() const { METRIC_INCR(group, aborted, 1); }
    void addBusyWake() const { METRIC_INCR(group, busyWake, 1); }
    void addConnect() const { METRIC_INCR(group, connect, 1); }
    void addConnectFailure() const { METRIC_INCR(group, connectFailure, 1); }
    void addCaptured(std::size_t bytes) const {
        METRIC_INCR(group, capturedBytes, static_cast<uint32_t>(bytes));
    }
    void addSent(std::size_t bytes) const {
        METRIC_INCR(group, sentBytes, static_cast<uint32_t>(bytes));
    }
    void addPlayed(std::size_t bytes) const {
        METRIC_INCR(group, playedBytes, static_cast<uint32_t>(bytes));
    }
    void addOverflow() const { METRIC_INCR(group, overflow, 1); }
    void addUnknownEvent() const { METRIC_INCR(group, unknownEvent, 1); }
    void addStatusFailure() const { METRIC_INCR(group, statusFailure, 1); }
    void addShortWrite() const { METRIC_INCR(group, shortWrite, 1); }
    void addPlaybackUnderflow() const {
        METRIC_INCR(group, playbackUnderflow, 1);
    }
    void addPlaybackOverflow() const {
        METRIC_INCR(group, playbackOverflow, 1);
    }
    void addEmptySample() const {
        METRIC_INCR(outcomeGroup, emptySample, 1);
    }
    void addEndpoint(CaptureEndReason reason) const {
        switch (reason) {
        case CaptureEndReason::VadSilence:
            METRIC_INCR(group, vadEnd, 1);
            break;
        case CaptureEndReason::NoSpeechTimeout:
            METRIC_INCR(group, noSpeechEnd, 1);
            break;
        case CaptureEndReason::MaximumDuration:
            METRIC_INCR(group, maximumEnd, 1);
            break;
        default:
            break;
        }
    }
    void setState(AssistantTurnState value) const {
        METRIC_SET(group, state, static_cast<uint32_t>(value));
    }
    void setCaptureHighWater(std::size_t bytes) const {
        METRIC_SET(group, captureHighWater, static_cast<uint32_t>(bytes));
    }
    void setConnectDuration(uint32_t durationMs) const {
        METRIC_SET(group, connectDuration, durationMs);
    }
    void setFirstAudioDuration(uint32_t durationMs) const {
        METRIC_SET(group, firstAudioDuration, durationMs);
    }
    void setPlaybackPrimeDuration(uint32_t durationMs) const {
        METRIC_SET(group, playbackPrimeDuration, durationMs);
    }
    void setTurnDuration(uint32_t durationMs) const {
        METRIC_SET(group, turnDuration, durationMs);
    }
    void setPlaybackQueueDepth(std::size_t bytes) const {
        METRIC_SET(group, playbackQueueDepth, static_cast<uint32_t>(bytes));
    }
    void setPlaybackQueueHighWater(std::size_t bytes) const {
        METRIC_SET(group, playbackQueueHighWater, static_cast<uint32_t>(bytes));
    }
    void setPlaybackWaitDuration(uint32_t durationMs) const {
        METRIC_SET(group, playbackWaitDuration, durationMs);
    }
    void setPlaybackWriteDuration(uint32_t durationMs) const {
        METRIC_SET(group, playbackWriteDuration, durationMs);
    }
    void setPlaybackBoundaryInput(uint32_t magnitude) const {
        METRIC_SET(group, playbackBoundaryInput, magnitude);
    }
    void setPlaybackBoundaryOutput(uint32_t magnitude) const {
        METRIC_SET(group, playbackBoundaryOutput, magnitude);
    }
    void setPlaybackBoundaryMuted(std::size_t samples) const {
        METRIC_SET(group, playbackBoundaryMuted,
                   static_cast<uint32_t>(samples));
    }

    GroupHandle group;
    GroupHandle outcomeGroup;
    CounterHandle started;
    CounterHandle committed;
    CounterHandle completed;
    CounterHandle aborted;
    CounterHandle busyWake;
    CounterHandle vadEnd;
    CounterHandle noSpeechEnd;
    CounterHandle maximumEnd;
    CounterHandle connect;
    CounterHandle connectFailure;
    CounterHandle capturedBytes;
    CounterHandle sentBytes;
    CounterHandle playedBytes;
    CounterHandle overflow;
    CounterHandle unknownEvent;
    CounterHandle statusFailure;
    CounterHandle shortWrite;
    CounterHandle playbackUnderflow;
    CounterHandle playbackOverflow;
    CounterHandle emptySample;
    GaugeHandle state;
    GaugeHandle captureHighWater;
    GaugeHandle connectDuration;
    GaugeHandle firstAudioDuration;
    GaugeHandle playbackPrimeDuration;
    GaugeHandle turnDuration;
    GaugeHandle playbackQueueDepth;
    GaugeHandle playbackQueueHighWater;
    GaugeHandle playbackWaitDuration;
    GaugeHandle playbackWriteDuration;
    GaugeHandle playbackBoundaryInput;
    GaugeHandle playbackBoundaryOutput;
    GaugeHandle playbackBoundaryMuted;

    static constexpr auto component =
        Totem::MetricsBackend::MetricComponent::Audio;
};

DEFINE_PREWARMED_METRICS_ACCESSORS(AssistantMetrics, assistantMetrics,
                                   prewarmAssistantMetrics)

} // namespace detail

class AssistantSession {
  public:
    DELETE_COPY(AssistantSession)
    DELETE_MOVE(AssistantSession)

    AssistantSession() = default;

    ReturnCode begin(Totem::Wifi::Wifi &wifi, Totem::AudioSink::I2SSink &sink,
                     AssistantSessionConfig config) {
        FAIL_IF(_active.load(), ERR(CoreError, InvalidState),
                "Assistant session is already active");
        FAIL_IF_NOT(config.validate(), ERR(CoreError, InvalidArgument),
                    "Invalid assistant session config");
        FAIL_IF(!sink.active(), ERR(CoreError, InvalidState),
                "Cannot begin assistant session before I2S output");

        detail::prewarmAssistantMetrics();
        _metrics = &detail::assistantMetrics();
        FAIL_IF_UNEXPECTED_FWD(
            listening,
            StatusLedService::directory().registerState(config.listeningStatus),
            "Failed to register Listening status LED state");
        FAIL_IF_UNEXPECTED_FWD(
            recording,
            StatusLedService::directory().registerState(config.recordingStatus),
            "Failed to register Recording status LED state");
        FAIL_IF_UNEXPECTED_FWD(
            playback,
            StatusLedService::directory().registerState(config.playbackStatus),
            "Failed to register Playback status LED state");

        _config = config;
        _wifi = &wifi;
        _sink = &sink;
        _listeningStatus = listening;
        _recordingStatus = recording;
        _playbackStatus = playback;
        FAIL_IF_ERR_FWD(
            _sink->setWriteTimeoutMs(_config.playbackWriteTimeoutMs),
            "Failed to configure assistant I2S write timeout");
        FAIL_IF_ERR_FWD(_webSocket.begin(_config.webSocket),
                        "Failed to initialize assistant WebSocket buffers");

        _captureRing = xRingbufferCreateWithCaps(
            _config.captureCapacityBytes, RINGBUF_TYPE_BYTEBUF,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        _playbackRing = xRingbufferCreateWithCaps(
            _config.playbackQueueCapacityBytes, RINGBUF_TYPE_BYTEBUF,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        _uploadBuffer = static_cast<std::byte *>(
            heap_caps_malloc(_config.webSocket.appendPcmBytes,
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        _playbackSilence = static_cast<std::byte *>(
            heap_caps_calloc(1, _config.playbackPrimeWriteBytes,
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (_captureRing == nullptr || _playbackRing == nullptr ||
            _uploadBuffer == nullptr || _playbackSilence == nullptr) {
            _releaseBuffers();
            FAIL(ERR(CoreError, OutOfMemory),
                 "Failed to allocate assistant audio buffers in PSRAM");
        }

        _active.store(true, std::memory_order_release);
        auto created = xTaskCreatePinnedToCoreWithCaps(
            AssistantSession::_playbackTaskEntry, _config.playbackTaskName,
            static_cast<configSTACK_DEPTH_TYPE>(_config.playbackTaskStackBytes),
            this, _config.playbackTaskPriority, &_playbackTask,
            _config.playbackTaskCore, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (created != pdPASS) {
            _active.store(false, std::memory_order_release);
            _releaseBuffers();
            FAIL(ERR(CoreError, OperationFailed),
                 "Failed to create assistant playback task");
        }

        created = xTaskCreatePinnedToCoreWithCaps(
            AssistantSession::_taskEntry, _config.taskName,
            static_cast<configSTACK_DEPTH_TYPE>(_config.taskStackBytes), this,
            _config.taskPriority, &_task, _config.taskCore,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (created != pdPASS) {
            _active.store(false, std::memory_order_release);
            xTaskNotifyGive(_playbackTask);
            _releaseBuffers();
            FAIL(ERR(CoreError, OperationFailed),
                 "Failed to create assistant network task");
        }

        _setState(AssistantTurnState::WaitingForWake);
        _log_i("Assistant session initialized: capture=%uB append=%uB "
               "playbackQueue=%uB prebuffer=%uB primeWrite=%uB "
               "networkTask=%s core=%d stack=%luB playbackTask=%s core=%d "
               "stack=%luB",
               static_cast<unsigned>(_config.captureCapacityBytes),
               static_cast<unsigned>(_config.webSocket.appendPcmBytes),
               static_cast<unsigned>(_config.playbackQueueCapacityBytes),
               static_cast<unsigned>(_config.playbackPrebufferBytes),
               static_cast<unsigned>(_config.playbackPrimeWriteBytes),
               _config.taskName, static_cast<int>(_config.taskCore),
               static_cast<unsigned long>(_config.taskStackBytes),
               _config.playbackTaskName,
               static_cast<int>(_config.playbackTaskCore),
               static_cast<unsigned long>(_config.playbackTaskStackBytes));
        return OK();
    }

    [[nodiscard]] Totem::AudioAfe::FrameSinkBinding binding() {
        return Totem::AudioAfe::FrameSinkBinding{
            .owner = this,
            .consumeFrame = AssistantSession::_consumeFrame,
            .pipelineStopped = AssistantSession::_pipelineStopped,
        };
    }

    [[nodiscard]] AssistantTurnState state() const {
        return _state.load(std::memory_order_acquire);
    }

  private:
    ReturnCode _consume(const Totem::AudioAfe::ProcessedFrameView &frame) {
        FAIL_IF(!_active.load(std::memory_order_acquire),
                ERR(CoreError, InvalidState),
                "Assistant session is not active");

        if (!_audioReady.exchange(true, std::memory_order_acq_rel)) {
            _log_i("Assistant audio pipeline ready");
            xTaskNotifyGive(_task);
        }

        auto current = state();
        if (frame.wakeDetected) {
            if (current == AssistantTurnState::WaitingForWake) {
                const auto *profile = _findWakeProfile(frame);
                if (profile == nullptr) {
                    _log_w("Ignoring unmapped WakeNet detection: model=%d "
                           "word=%d",
                           frame.wakeNetModelIndex, frame.wakeWordIndex);
                } else {
                    _startTurn(frame, *profile);
                }
            } else {
                _metrics->addBusyWake();
                _log_i("WakeNet detection ignored during " SV_FMT
                       ": model=%d word=%d",
                       MAGIC_SV_ARG(current), frame.wakeNetModelIndex,
                       frame.wakeWordIndex);
            }
        }

        if (state() != AssistantTurnState::Recording) {
            return OK();
        }

        _capture(frame.samples);
        if (state() == AssistantTurnState::Recording) {
            _updateEndpoint(frame);
        }
        return OK();
    }

    [[nodiscard]] const AssistantWakeProfile *
    _findWakeProfile(const Totem::AudioAfe::ProcessedFrameView &frame) const {
        for (const auto &profile : _config.wakeProfiles) {
            if (frame.wakeNetModelIndex == profile.wakeNetModelIndex &&
                frame.wakeWordIndex == profile.wakeWordIndex) {
                return &profile;
            }
        }
        return nullptr;
    }

    void _startTurn(const Totem::AudioAfe::ProcessedFrameView &frame,
                    const AssistantWakeProfile &profile) {
        _discardCapture();
        _captureEnd.store(CaptureEndReason::None, std::memory_order_release);
        _captureBytes.store(0, std::memory_order_relaxed);
        _captureHighWater.store(0, std::memory_order_relaxed);
        _captureWriters.store(0, std::memory_order_relaxed);
        _captureEndedMs.store(0, std::memory_order_relaxed);
        _activeProfile = profile;
        _turnStartedMs = frame.timestampMs;
        _speechObserved = false;
        _hasLastVad = false;
        _acceptingPcm.store(true, std::memory_order_release);
        _setRecordingStatus(true);
        _setState(AssistantTurnState::Recording);
        _metrics->addStarted();
        _log_i("Recording started by WakeNet: requestId=wake-%lu model=%d "
               "word=%d wakeWord=" SV_FMT " voice=" SV_FMT
               " volume=%.1fdB",
               static_cast<unsigned long>(_turnStartedMs),
               frame.wakeNetModelIndex, frame.wakeWordIndex,
               SV_ARG(_activeProfile.wakeWord), SV_ARG(_activeProfile.voice),
               static_cast<double>(frame.inputVolumeDb));
        xTaskNotifyGive(_task);
    }

    void _capture(std::span<const int16_t> samples) {
        if (!_acceptingPcm.load(std::memory_order_acquire) || samples.empty()) {
            return;
        }

        _captureWriters.fetch_add(1, std::memory_order_acq_rel);
        if (!_acceptingPcm.load(std::memory_order_acquire)) {
            _captureWriters.fetch_sub(1, std::memory_order_acq_rel);
            return;
        }

        const auto sent = xRingbufferSend(_captureRing, samples.data(),
                                          samples.size_bytes(), 0);
        _captureWriters.fetch_sub(1, std::memory_order_acq_rel);
        if (sent != pdTRUE) {
            _metrics->addOverflow();
            _finishCapture(CaptureEndReason::CaptureOverflow,
                           ::platform::get_time());
            return;
        }

        _captureBytes.fetch_add(static_cast<uint32_t>(samples.size_bytes()),
                                std::memory_order_relaxed);
        const auto used = _config.captureCapacityBytes -
                          xRingbufferGetCurFreeSize(_captureRing);
        auto highWater = _captureHighWater.load(std::memory_order_relaxed);
        while (used > highWater &&
               !_captureHighWater.compare_exchange_weak(
                   highWater, used, std::memory_order_relaxed)) {
        }
    }

    void _updateEndpoint(const Totem::AudioAfe::ProcessedFrameView &frame) {
        if (frame.vad == Totem::AudioAfe::VadState::Speech) {
            if (!_speechObserved) {
                _speechObserved = true;
                _log_i("Post-wake VAD detected command speech");
            }
        } else if (_speechObserved && _hasLastVad &&
                   _lastVad == Totem::AudioAfe::VadState::Speech) {
            _finishCapture(CaptureEndReason::VadSilence, frame.timestampMs);
            return;
        }
        _lastVad = frame.vad;
        _hasLastVad = true;

        if (!_speechObserved && _elapsed(frame.timestampMs, _turnStartedMs,
                                         _config.noSpeechTimeoutMs)) {
            _finishCapture(CaptureEndReason::NoSpeechTimeout,
                           frame.timestampMs);
            return;
        }
        if (_elapsed(frame.timestampMs, _turnStartedMs,
                     _config.maximumSessionMs)) {
            _finishCapture(CaptureEndReason::MaximumDuration,
                           frame.timestampMs);
        }
    }

    void _finishCapture(CaptureEndReason reason, uint32_t nowMs) {
        auto expected = CaptureEndReason::None;
        if (!_captureEnd.compare_exchange_strong(expected, reason,
                                                 std::memory_order_acq_rel)) {
            return;
        }
        _captureEndedMs.store(nowMs, std::memory_order_release);
        _acceptingPcm.store(false, std::memory_order_release);
        _clearListeningStatus();
        _setRecordingStatus(false);
        _setState(AssistantTurnState::AwaitingResponse);
        _metrics->addEndpoint(reason);
        _log_i("Recording stopped: reason=" SV_FMT " duration=%lums",
               MAGIC_SV_ARG(reason),
               static_cast<unsigned long>(nowMs - _turnStartedMs));
        xTaskNotifyGive(_task);
    }

    ReturnCode _stopPipeline() {
        if (_audioReady.exchange(false, std::memory_order_acq_rel)) {
            _log_w("Assistant audio pipeline stopped");
            xTaskNotifyGive(_task);
        }
        if (state() == AssistantTurnState::Recording) {
            _finishCapture(CaptureEndReason::PipelineStop,
                           ::platform::get_time());
        }
        return OK();
    }

    void _runTask() {
        _log_i("Assistant network task started on core %d",
               static_cast<int>(xPortGetCoreID()));
        while (_active.load(std::memory_order_acquire)) {
            (void)ulTaskNotifyTake(
                pdTRUE, ::platform::ms_to_ticks(_config.readinessPollMs));
            _updateNetworkReadiness();
            const auto current = state();
            if (current != AssistantTurnState::Recording &&
                current != AssistantTurnState::AwaitingResponse) {
                continue;
            }

            _turnCompleted = false;
            _turnEmptySample = false;
            _turnCommitted = false;
            _turnAbortReason = TurnAbortReason::None;
            _sentBytes = 0;
            _playedBytes.store(0, std::memory_order_relaxed);
            _responseBytes = 0;
            _uploadSize = 0;
            _uploadAppendCount = 0;
            _uploadSendTotalMs = 0;
            _uploadSendMaxMs = 0;
            _playbackQueueHighWater.store(0, std::memory_order_relaxed);
            _playbackWaitMaxMs.store(0, std::memory_order_relaxed);
            _playbackWriteMaxMs.store(0, std::memory_order_relaxed);
            _playbackUnderflows.store(0, std::memory_order_relaxed);
            _playbackBoundaryMutedSamples = 0;
            _playbackBoundaryInputMagnitude = 0;
            _playbackBoundaryPending = _config.playbackStartAtZeroCrossing;
            _playbackBoundaryInputObserved = false;
            _playbackProducerDone.store(false, std::memory_order_release);
            _playbackAbort.store(false, std::memory_order_release);
            _playbackFailed.store(false, std::memory_order_release);
            _playbackWorkerComplete.store(true, std::memory_order_release);
            _playbackAudioReady.store(false, std::memory_order_release);
            _playbackPrimeWritesEnabled.store(false, std::memory_order_release);
            _playbackFirstWritePending.store(true, std::memory_order_release);
            _audioAnnouncedMs.store(0, std::memory_order_relaxed);
            _playbackStarted = false;
            _playbackPrimingStarted = false;
            _playbackPrimeStartedMs = 0;
            _playbackStartedMs = 0;
            _responseSampleRate = 0;
            _discardPlayback();
            _metrics->setPlaybackQueueDepth(0);
            _metrics->setPlaybackQueueHighWater(0);
            _metrics->setPlaybackPrimeDuration(0);
            _metrics->setPlaybackWaitDuration(0);
            _metrics->setPlaybackWriteDuration(0);
            _metrics->setPlaybackBoundaryInput(0);
            _metrics->setPlaybackBoundaryOutput(0);
            _metrics->setPlaybackBoundaryMuted(0);
            const auto result = _processTurn();
            if (!result.ok()) {
                _finishCapture(CaptureEndReason::TransportFailure,
                               ::platform::get_time());
                if (_turnAbortReason == TurnAbortReason::None) {
                    _turnAbortReason = TurnAbortReason::Transport;
                }
            }
            if (_turnCompleted) {
                _metrics->addCompleted();
            } else if (_turnEmptySample) {
                _metrics->addEmptySample();
            } else {
                _metrics->addAborted();
                if (result.ok()) {
                    _log_w("Assistant turn aborted: reason=" SV_FMT,
                           MAGIC_SV_ARG(_turnAbortReason));
                } else {
                    _log_e("Assistant turn aborted: reason=" SV_FMT " " ERR_FMT,
                           MAGIC_SV_ARG(_turnAbortReason), ERR_ARG(result));
                }
            }
            _cleanupTurn();
        }
        _task = nullptr;
        vTaskDeleteWithCaps(nullptr);
    }

    void _runPlaybackTask() {
        _log_i("Assistant playback task started on core %d",
               static_cast<int>(xPortGetCoreID()));
        while (_active.load(std::memory_order_acquire)) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            if (!_active.load(std::memory_order_acquire)) {
                break;
            }
            _consumePlaybackQueue();
        }
        _playbackTask = nullptr;
        vTaskDeleteWithCaps(nullptr);
    }

    void _consumePlaybackQueue() {
        bool waitingForProducer = false;
        uint32_t waitStartedMs = 0;

        while (!_playbackAbort.load(std::memory_order_acquire)) {
            if (!_playbackAudioReady.load(std::memory_order_acquire)) {
                if (!_playbackPrimeWritesEnabled.load(
                        std::memory_order_acquire)) {
                    (void)ulTaskNotifyTake(
                        pdTRUE,
                        ::platform::ms_to_ticks(_config.playbackQueuePollMs));
                    continue;
                }
                const auto result = _writePlaybackSilence();
                if (!result.ok()) {
                    _playbackFailed.store(true, std::memory_order_release);
                    _playbackAbort.store(true, std::memory_order_release);
                    _log_e("Assistant playback priming write failed: " ERR_FMT,
                           ERR_ARG(result));
                    break;
                }
                continue;
            }

            if (waitingForProducer) {
                const auto depth = _playbackQueueDepth();
                const auto producerDone =
                    _playbackProducerDone.load(std::memory_order_acquire);
                if (!producerDone && depth < _config.playbackPrebufferBytes) {
                    (void)ulTaskNotifyTake(
                        pdTRUE,
                        ::platform::ms_to_ticks(_config.playbackQueuePollMs));
                    continue;
                }

                const auto waitedMs = ::platform::get_time() - waitStartedMs;
                _recordPlaybackWait(waitedMs);
                waitingForProducer = false;
                if (depth == 0) {
                    break;
                }
                _log_i("Assistant playback rebuffered: queued=%uB waited=%lums",
                       static_cast<unsigned>(depth),
                       static_cast<unsigned long>(waitedMs));
            }

            std::size_t bytes = 0;
            auto *item = xRingbufferReceiveUpTo(
                _playbackRing, &bytes,
                ::platform::ms_to_ticks(_config.playbackQueuePollMs),
                _config.playbackWriteBytes);
            const auto nowMs = ::platform::get_time();
            if (item == nullptr) {
                if (_playbackProducerDone.load(std::memory_order_acquire)) {
                    break;
                }
                if (!waitingForProducer) {
                    waitingForProducer = true;
                    waitStartedMs = nowMs;
                    _playbackUnderflows.fetch_add(1, std::memory_order_relaxed);
                    _metrics->addPlaybackUnderflow();
                }
                continue;
            }

            const auto result = _writePlayback(
                std::span<std::byte>{static_cast<std::byte *>(item), bytes});
            vRingbufferReturnItem(_playbackRing, item);
            _updatePlaybackQueueMetrics();
            if (!result.ok()) {
                _playbackFailed.store(true, std::memory_order_release);
                _playbackAbort.store(true, std::memory_order_release);
                _log_e("Assistant playback worker write failed: " ERR_FMT,
                       ERR_ARG(result));
                break;
            }
        }

        if (waitingForProducer) {
            _recordPlaybackWait(::platform::get_time() - waitStartedMs);
        }
        _playbackWorkerComplete.store(true, std::memory_order_release);
        if (_task != nullptr) {
            xTaskNotifyGive(_task);
        }
    }

    ReturnCode _processTurn() {
        const auto captureEnd = _captureEnd.load(std::memory_order_acquire);
        if (!_commitEligible(captureEnd) &&
            captureEnd != CaptureEndReason::None) {
            _turnAbortReason = _abortReason(captureEnd);
            return OK();
        }

        if (!_wifiReady()) {
            _finishCapture(CaptureEndReason::NetworkUnavailable,
                           ::platform::get_time());
            _turnAbortReason = TurnAbortReason::NetworkUnavailable;
            return OK();
        }
        if (!_clockReady()) {
            _finishCapture(CaptureEndReason::ClockUnavailable,
                           ::platform::get_time());
            _turnAbortReason = TurnAbortReason::ClockUnavailable;
            return OK();
        }

        _metrics->addConnect();
        const auto connectStartedMs = ::platform::get_time();
        auto result = _webSocket.connect(_turnStartedMs, _turnStartedEpochMs());
        const auto connectDurationMs =
            ::platform::get_time() - connectStartedMs;
        _metrics->setConnectDuration(connectDurationMs);
        if (!result.ok()) {
            _metrics->addConnectFailure();
            _finishCapture(CaptureEndReason::TransportFailure,
                           ::platform::get_time());
            _turnAbortReason = TurnAbortReason::Transport;
            return result;
        }
        _log_i("Assistant WebSocket connected in %lums",
               static_cast<unsigned long>(connectDurationMs));

        result = _webSocket.sendSessionUpdate(_activeProfile.voice);
        if (!result.ok()) {
            _finishCapture(CaptureEndReason::TransportFailure,
                           ::platform::get_time());
            _turnAbortReason = TurnAbortReason::Transport;
            return result;
        }
        result = _uploadCommand();
        if (!result.ok() || !_turnCommitted) {
            return result;
        }
        return _receiveResponse();
    }

    ReturnCode _uploadCommand() {
        while (_captureEnd.load(std::memory_order_acquire) ==
               CaptureEndReason::None) {
            FAIL_IF_ERR_FWD(_receiveUploadEvent(),
                            "Assistant upload receive failed");
            _receiveCapture(_config.uploadPollMs);
            if (_uploadSize == _config.webSocket.appendPcmBytes) {
                FAIL_IF_ERR_FWD(_sendUploadBuffer(),
                                "Assistant audio append failed");
            }
        }

        const auto finalDrainStartedMs = ::platform::get_time();
        const auto sentBeforeFinalDrain = _sentBytes;
        FAIL_IF_NOT(_waitForCaptureWriters(), ERR(CoreError, Timeout),
                    "Assistant capture writer did not quiesce");
        while (_receiveCapture(0)) {
            if (_uploadSize == _config.webSocket.appendPcmBytes) {
                FAIL_IF_ERR_FWD(_sendUploadBuffer(),
                                "Assistant final audio append failed");
            }
        }

        const auto endReason = _captureEnd.load(std::memory_order_acquire);
        if (!_commitEligible(endReason) || !_speechObserved) {
            _turnAbortReason = _abortReason(endReason);
            return OK();
        }
        FAIL_IF_ERR_FWD(_sendUploadBuffer(),
                        "Assistant partial audio append failed");
        const auto finalDrainCompletedMs = ::platform::get_time();
        const auto commitStartedMs = ::platform::get_time();
        FAIL_IF_ERR_FWD(_webSocket.sendCommit(),
                        "Assistant input commit failed");
        _turnCommitted = true;
        _commitMs = ::platform::get_time();
        _metrics->addCommitted();
        const auto captureEndedMs =
            _captureEndedMs.load(std::memory_order_acquire);
        const auto endpointToCommitMs =
            captureEndedMs == 0 ? 0 : _commitMs - captureEndedMs;
        _log_i("Assistant input committed: pcm=%luB duration=%lums "
               "endpointToCommit=%lums finalPcm=%luB",
               static_cast<unsigned long>(_sentBytes),
               static_cast<unsigned long>(_commitMs - _turnStartedMs),
               static_cast<unsigned long>(endpointToCommitMs),
               static_cast<unsigned long>(_sentBytes - sentBeforeFinalDrain));
        _log_i("Assistant input upload timing: finalDrain=%lums appends=%lu "
               "appendSend=%lums appendMax=%lums commitWrite=%lums",
               static_cast<unsigned long>(finalDrainCompletedMs -
                                          finalDrainStartedMs),
               static_cast<unsigned long>(_uploadAppendCount),
               static_cast<unsigned long>(_uploadSendTotalMs),
               static_cast<unsigned long>(_uploadSendMaxMs),
               static_cast<unsigned long>(_commitMs - commitStartedMs));
        FAIL_IF_ERR_FWD(_startPlaybackPriming(),
                        "Failed to start assistant playback priming");
        return OK();
    }

    bool _receiveCapture(uint32_t waitMs) {
        if (_uploadSize >= _config.webSocket.appendPcmBytes) {
            return false;
        }
        std::size_t receivedBytes = 0;
        auto *received = xRingbufferReceiveUpTo(
            _captureRing, &receivedBytes, ::platform::ms_to_ticks(waitMs),
            _config.webSocket.appendPcmBytes - _uploadSize);
        if (received == nullptr) {
            return false;
        }
        std::memcpy(_uploadBuffer + _uploadSize, received, receivedBytes);
        _uploadSize += receivedBytes;
        vRingbufferReturnItem(_captureRing, received);
        return true;
    }

    ReturnCode _sendUploadBuffer() {
        if (_uploadSize == 0) {
            return OK();
        }
        const auto sendStartedMs = ::platform::get_time();
        const auto result = _webSocket.sendAudio(
            std::span<const std::byte>{_uploadBuffer, _uploadSize});
        const auto sendMs = ::platform::get_time() - sendStartedMs;
        _uploadSendTotalMs += sendMs;
        _uploadSendMaxMs = std::max(_uploadSendMaxMs, sendMs);
        if (!result.ok()) {
            _turnAbortReason = TurnAbortReason::Transport;
            return result;
        }
        ++_uploadAppendCount;
        _sentBytes += _uploadSize;
        _metrics->addSent(_uploadSize);
        _uploadSize = 0;
        return OK();
    }

    ReturnCode _receiveUploadEvent() {
        AssistantServerEvent event{};
        const auto result = _webSocket.receive(event, 1);
        if (!result.ok()) {
            _turnAbortReason = TurnAbortReason::Transport;
            return result;
        }
        switch (event.type) {
        case AssistantServerEventType::None:
            return OK();
        case AssistantServerEventType::Unknown:
            _metrics->addUnknownEvent();
            return OK();
        case AssistantServerEventType::Error:
        case AssistantServerEventType::Closed:
            _turnAbortReason = TurnAbortReason::Protocol;
            return ERR(CoreError, InvalidResponse);
        default:
            _turnAbortReason = TurnAbortReason::Protocol;
            return ERR(CoreError, InvalidData);
        }
    }

    ReturnCode _receiveResponse() {
        bool audioStarted = false;
        bool audioDone = false;
        bool responseDone = false;
        const auto responseStartedMs = ::platform::get_time();
        while (!_elapsed(::platform::get_time(), responseStartedMs,
                         _config.responseTimeoutMs)) {
            AssistantServerEvent event{};
            auto result =
                _webSocket.receive(event, _config.webSocket.readTimeoutMs);
            if (!result.ok()) {
                _turnAbortReason = TurnAbortReason::Transport;
                return result;
            }
            switch (event.type) {
            case AssistantServerEventType::None:
                break;
            case AssistantServerEventType::Unknown:
                _metrics->addUnknownEvent();
                break;
            case AssistantServerEventType::Error:
                _turnAbortReason = TurnAbortReason::Protocol;
                _log_w("Assistant server returned an error event");
                return ERR(CoreError, InvalidResponse);
            case AssistantServerEventType::AudioStarted:
                if (audioStarted) {
                    _turnAbortReason = TurnAbortReason::Protocol;
                    _log_w("Assistant sent a duplicate audio-start event");
                    return ERR(CoreError, InvalidData);
                }
                result = _startPlayback(event);
                if (!result.ok()) {
                    return result;
                }
                audioStarted = true;
                break;
            case AssistantServerEventType::AudioDelta:
                if (!audioStarted || audioDone) {
                    _turnAbortReason = TurnAbortReason::Protocol;
                    _log_w("Assistant sent audio outside its announced stream");
                    return ERR(CoreError, InvalidData);
                }
                result = _play(event.audio);
                if (!result.ok()) {
                    return result;
                }
                break;
            case AssistantServerEventType::AudioDone:
                if (!audioStarted || audioDone) {
                    _turnAbortReason = TurnAbortReason::Protocol;
                    _log_w("Assistant sent an unexpected audio-done event");
                    return ERR(CoreError, InvalidData);
                }
                result = _finishPlayback();
                if (!result.ok()) {
                    _turnAbortReason = TurnAbortReason::Playback;
                    return result;
                }
                audioDone = true;
                break;
            case AssistantServerEventType::ResponseDone:
                responseDone = true;
                _log_i("Assistant response-done event received");
                break;
            case AssistantServerEventType::Closed:
                _log_i("Assistant close state: code=%u audio=%s audioDone=%s "
                       "responseDone=%s",
                       static_cast<unsigned>(event.closeCode),
                       audioStarted ? "started" : "missing",
                       audioDone ? "true" : "false",
                       responseDone ? "true" : "false");
                // The server reserves policy violation for an empty submitted
                // sample, which is expected after an accidental wake word.
                if (event.closeCode ==
                    AssistantWebSocket::policyViolationCode) {
                    _turnEmptySample = true;
                    _log_i("Assistant server ignored an empty sample");
                    return OK();
                }
                if (event.closeCode != AssistantWebSocket::normalClosureCode ||
                    !audioStarted || !audioDone ||
                    !responseDone) {
                    _turnAbortReason = TurnAbortReason::Protocol;
                    return ERR(CoreError, InvalidResponse);
                }
                _turnCompleted = true;
                _log_i("Assistant response completed with normal close");
                return OK();
            }
        }
        _turnAbortReason = TurnAbortReason::ResponseTimeout;
        return ERR(CoreError, Timeout);
    }

    ReturnCode _startPlayback(const AssistantServerEvent &event) {
        if (event.sampleWidth !=
                _config.responseAudio.bitsPerSample / UINT8_C(8) ||
            event.channels != _config.responseAudio.channels ||
            event.sampleRate != _config.responseAudio.sampleRate) {
            _turnAbortReason = TurnAbortReason::Protocol;
            return ERR(CoreError, InvalidData);
        }
        auto result = _playbackResampler.reset();
        if (!result.ok()) {
            _turnAbortReason = TurnAbortReason::Playback;
            return result;
        }
        if (!_playbackPrimingStarted) {
            _turnAbortReason = TurnAbortReason::Playback;
            FAIL(ERR(CoreError, InvalidState),
                 "Assistant response arrived before I2S priming started");
        }
        if (_playbackFailed.load(std::memory_order_acquire)) {
            _turnAbortReason = TurnAbortReason::Playback;
            FAIL(ERR(CoreError, OperationFailed),
                 "Assistant I2S priming failed before response audio");
        }
        _responseSampleRate = _config.playbackAudio.sampleRate;
        _setState(AssistantTurnState::PlayingResponse);
        const auto announcedMs = ::platform::get_time();
        _audioAnnouncedMs.store(announcedMs, std::memory_order_release);
        _playbackPrimeWritesEnabled.store(false, std::memory_order_release);
        xTaskNotifyGive(_playbackTask);
        const auto firstAudioMs = announcedMs - _commitMs;
        _metrics->setFirstAudioDuration(firstAudioMs);
        _log_i("Assistant response audio announced: %luHz -> %luHz mono "
               "PCM16 latency=%lums",
               static_cast<unsigned long>(event.sampleRate),
               static_cast<unsigned long>(_responseSampleRate),
               static_cast<unsigned long>(firstAudioMs));
        return OK();
    }

    ReturnCode _play(std::span<std::byte> audio) {
        _responseBytes += audio.size();
        auto input = std::span<const int16_t>{
            reinterpret_cast<const int16_t *>(audio.data()),
            audio.size() / sizeof(int16_t)};
        while (!input.empty()) {
            const auto samples = std::min(
                input.size(), Pcm16PlaybackResampler::maximumInputSamples);
            std::span<int16_t> output{};
            auto result =
                _playbackResampler.process(input.first(samples), output);
            if (!result.ok()) {
                _turnAbortReason = TurnAbortReason::Playback;
                FAIL_ERR_FWD(result,
                             "Failed to resample assistant playback audio");
            }
            result = _queuePlayback(std::as_writable_bytes(output));
            if (!result.ok()) {
                _turnAbortReason = TurnAbortReason::Playback;
                FAIL_ERR_FWD(
                    result,
                    "Failed to queue resampled assistant playback audio");
            }
            input = input.subspan(samples);
        }
        return OK();
    }

    ReturnCode _queuePlayback(std::span<std::byte> audio) {
        FAIL_IF(audio.empty(), ERR(CoreError, InvalidArgument),
                "Assistant playback queue received empty audio");
        FAIL_IF(_playbackFailed.load(std::memory_order_acquire),
                ERR(CoreError, OperationFailed),
                "Assistant playback worker has failed");

        const auto sent = xRingbufferSend(
            _playbackRing, audio.data(), audio.size(),
            ::platform::ms_to_ticks(_config.playbackQueueWriteTimeoutMs));
        if (sent != pdTRUE) {
            _metrics->addPlaybackOverflow();
            _playbackFailed.store(true, std::memory_order_release);
            _log_e("Assistant playback queue overflow: depth=%uB capacity=%uB",
                   static_cast<unsigned>(_playbackQueueDepth()),
                   static_cast<unsigned>(_config.playbackQueueCapacityBytes));
            return ERR(CoreError, OperationFailed);
        }

        const auto depth = _updatePlaybackQueueMetrics();
        if (_playbackStarted && _playbackTask != nullptr) {
            xTaskNotifyGive(_playbackTask);
        }
        if (!_playbackStarted && depth >= _config.playbackPrebufferBytes) {
            FAIL_IF_ERR_FWD(_startPlaybackAudio(),
                            "Failed to start assistant response audio");
        }
        return OK();
    }

    ReturnCode _startPlaybackPriming() {
        FAIL_IF(_playbackPrimingStarted, ERR(CoreError, InvalidState),
                "Assistant playback priming is already active");
        FAIL_IF(_playbackTask == nullptr, ERR(CoreError, InvalidState),
                "Assistant playback task is unavailable");
        FAIL_IF_NOT(_playbackWorkerComplete.load(std::memory_order_acquire),
                    ERR(CoreError, InvalidState),
                    "Assistant playback worker is still active");
        FAIL_IF_ERR_FWD(_sink->setAudioInfo(_config.playbackAudio),
                        "Failed to configure primed I2S output");

        _responseSampleRate = _config.playbackAudio.sampleRate;
        _playbackPrimingStarted = true;
        _playbackPrimeStartedMs = ::platform::get_time();
        _playbackPrimeWritesEnabled.store(true, std::memory_order_release);
        _playbackWorkerComplete.store(false, std::memory_order_release);
        _log_i("Assistant playback priming started: silenceWrite=%uB",
               static_cast<unsigned>(_config.playbackPrimeWriteBytes));
        xTaskNotifyGive(_playbackTask);
        return OK();
    }

    ReturnCode _startPlaybackAudio() {
        FAIL_IF(_playbackStarted, ERR(CoreError, InvalidState),
                "Assistant response audio is already running");
        FAIL_IF_NOT(_playbackPrimingStarted, ERR(CoreError, InvalidState),
                    "Assistant playback priming is not active");
        FAIL_IF(_playbackWorkerComplete.load(std::memory_order_acquire),
                ERR(CoreError, InvalidState),
                "Assistant playback worker stopped during priming");
        FAIL_IF(_playbackQueueDepth() == 0, ERR(CoreError, InvalidData),
                "Assistant playback queue is empty");

        _playbackStarted = true;
        _playbackStartedMs = ::platform::get_time();
        const auto primeMs = _playbackStartedMs - _playbackPrimeStartedMs;
        _metrics->setPlaybackPrimeDuration(primeMs);
        _setInformationalStatus(AssistantInformationalStatus::Playback);
        _log_i("Assistant playback started: queued=%uB prebuffer=%uB "
               "primed=%lums",
               static_cast<unsigned>(_playbackQueueDepth()),
               static_cast<unsigned>(_config.playbackPrebufferBytes),
               static_cast<unsigned long>(primeMs));
        _playbackAudioReady.store(true, std::memory_order_release);
        xTaskNotifyGive(_playbackTask);
        return OK();
    }

    ReturnCode _writePlaybackSilence() {
        return _writeSink(
            std::span<const std::byte>{_playbackSilence,
                                       _config.playbackPrimeWriteBytes},
            false);
    }

    static uint32_t _sampleMagnitude(int16_t sample) {
        const auto value = static_cast<int32_t>(sample);
        return static_cast<uint32_t>(value < 0 ? -value : value);
    }

    void _conditionPlaybackBoundary(std::span<int16_t> samples) {
        if (!_playbackBoundaryPending || samples.empty()) {
            return;
        }

        const auto first =
            std::find_if(samples.begin(), samples.end(),
                         [](int16_t sample) { return sample != 0; });
        if (first == samples.end()) {
            return;
        }

        if (!_playbackBoundaryInputObserved) {
            _playbackBoundaryInputMagnitude = _sampleMagnitude(*first);
            _playbackBoundaryInputObserved = true;
            _metrics->setPlaybackBoundaryInput(_playbackBoundaryInputMagnitude);
        }

        auto boundary = std::span<int16_t>{
            first, static_cast<std::size_t>(samples.end() - first)};
        audio_tools::PoppingSoundRemover<int16_t> remover{1, true, false};
        (void)remover.convert(reinterpret_cast<uint8_t *>(boundary.data()),
                              boundary.size_bytes());

        const auto output =
            std::find_if(boundary.begin(), boundary.end(),
                         [](int16_t sample) { return sample != 0; });
        _playbackBoundaryMutedSamples +=
            static_cast<std::size_t>(output - boundary.begin());
        _metrics->setPlaybackBoundaryMuted(_playbackBoundaryMutedSamples);
        if (output == boundary.end()) {
            return;
        }

        const auto outputMagnitude = _sampleMagnitude(*output);
        _metrics->setPlaybackBoundaryOutput(outputMagnitude);
        _playbackBoundaryPending = false;
        _log_i("Assistant playback boundary aligned to zero crossing: "
               "input=%lu output=%lu muted=%u samples",
               static_cast<unsigned long>(_playbackBoundaryInputMagnitude),
               static_cast<unsigned long>(outputMagnitude),
               static_cast<unsigned>(_playbackBoundaryMutedSamples));
    }

    ReturnCode _writePlayback(std::span<std::byte> audio) {
        auto samples =
            std::span<int16_t>{reinterpret_cast<int16_t *>(audio.data()),
                               audio.size() / sizeof(int16_t)};
        _conditionPlaybackBoundary(samples);
        if (_config.responseGain != 1.0F) {
            for (auto &sample : samples) {
                const auto scaled = std::lround(static_cast<float>(sample) *
                                                _config.responseGain);
                sample = static_cast<int16_t>(std::clamp<long>(
                    scaled, std::numeric_limits<int16_t>::min(),
                    std::numeric_limits<int16_t>::max()));
            }
        }
        const auto firstNonzeroWrite =
            _playbackFirstWritePending.load(std::memory_order_acquire) &&
            std::ranges::any_of(samples,
                                [](int16_t sample) { return sample != 0; });
        const auto writeStartedMs = ::platform::get_time();
        const auto result = _writeSink(audio, true);
        if (result.ok() && firstNonzeroWrite) {
            _playbackFirstWritePending.store(false, std::memory_order_release);
            const auto announcedMs =
                _audioAnnouncedMs.load(std::memory_order_acquire);
            _log_i("Assistant first nonzero I2S write started: "
                   "announcement=%lums",
                   static_cast<unsigned long>(writeStartedMs - announcedMs));
        }
        return result;
    }

    ReturnCode _writeSink(std::span<const std::byte> audio,
                          bool countAsResponseAudio) {
        std::size_t offset = 0;
        while (offset < audio.size()) {
            const auto bytes =
                std::min(_config.playbackWriteBytes, audio.size() - offset);
            const auto writeStartedMs = ::platform::get_time();
            const auto written = _sink->stream().write(
                reinterpret_cast<const uint8_t *>(audio.data() + offset),
                bytes);
            const auto writeMs = ::platform::get_time() - writeStartedMs;
            _updateMaximum(_playbackWriteMaxMs, writeMs);
            _metrics->setPlaybackWriteDuration(
                _playbackWriteMaxMs.load(std::memory_order_relaxed));
            if (written != bytes) {
                _metrics->addShortWrite();
                return ERR(CoreError, OperationFailed);
            }
            offset += bytes;
            if (countAsResponseAudio) {
                _playedBytes.fetch_add(bytes, std::memory_order_relaxed);
                _metrics->addPlayed(bytes);
            }
        }
        return OK();
    }

    ReturnCode _finishPlayback() {
        FAIL_IF(_responseBytes == 0, ERR(CoreError, InvalidData),
                "Assistant completed an empty audio response");

        _playbackProducerDone.store(true, std::memory_order_release);
        if (_playbackTask != nullptr) {
            xTaskNotifyGive(_playbackTask);
        }
        if (!_playbackStarted) {
            FAIL_IF_ERR_FWD(_startPlaybackAudio(),
                            "Failed to start short assistant response audio");
        }
        FAIL_IF_NOT(_waitForPlaybackWorker(_config.playbackCompletionTimeoutMs),
                    ERR(CoreError, Timeout),
                    "Assistant playback worker did not complete");
        FAIL_IF(_playbackFailed.load(std::memory_order_acquire),
                ERR(CoreError, OperationFailed),
                "Assistant playback worker failed");

        const auto dmaBytes = std::max(0, _sink->stream().availableForWrite());
        const auto bytesPerSecond =
            static_cast<uint64_t>(_responseSampleRate) * sizeof(int16_t);
        const auto drainMs = static_cast<uint32_t>(
            (static_cast<uint64_t>(dmaBytes) * 1000U + bytesPerSecond - 1U) /
            bytesPerSecond);
        ::platform::delay(
            ::platform::ms_to_ticks(drainMs + _config.playbackDrainMarginMs));

        const auto wallMs = ::platform::get_time() - _playbackStartedMs;
        const auto playedBytes = _playedBytes.load(std::memory_order_relaxed);
        const auto pcmMs = static_cast<uint32_t>(
            (static_cast<uint64_t>(playedBytes) * 1000U) / bytesPerSecond);
        _log_i("Assistant response audio complete: source=%luB output=%luB "
               "pcm=%lums wall=%lums queueHigh=%uB underflows=%lu "
               "waitMax=%lums writeMax=%lums",
               static_cast<unsigned long>(_responseBytes),
               static_cast<unsigned long>(playedBytes),
               static_cast<unsigned long>(pcmMs),
               static_cast<unsigned long>(wallMs),
               static_cast<unsigned>(
                   _playbackQueueHighWater.load(std::memory_order_relaxed)),
               static_cast<unsigned long>(
                   _playbackUnderflows.load(std::memory_order_relaxed)),
               static_cast<unsigned long>(
                   _playbackWaitMaxMs.load(std::memory_order_relaxed)),
               static_cast<unsigned long>(
                   _playbackWriteMaxMs.load(std::memory_order_relaxed)));
        return OK();
    }

    void _cleanupTurn() {
        _acceptingPcm.store(false, std::memory_order_release);
        if (!_waitForCaptureWriters()) {
            REPORT_IF_ERR(ERR(CoreError, Timeout),
                          "Assistant capture writers did not stop during "
                          "cleanup");
        }
        _stopPlayback();
        if (_webSocket.connected()) {
            _webSocket.close();
        } else {
            _webSocket.disconnect();
        }
        _setRecordingStatus(false);
        _discardCapture();
        _metrics->addCaptured(_captureBytes.load(std::memory_order_relaxed));
        _metrics->setCaptureHighWater(
            _captureHighWater.load(std::memory_order_relaxed));
        _metrics->setTurnDuration(::platform::get_time() - _turnStartedMs);
        _activeProfile = {};
        _setState(AssistantTurnState::WaitingForWake);
        _promoteListeningIfReady();
        _log_i("Assistant returned to wake-word detection");
    }

    void _updateNetworkReadiness() {
        const auto wifiReady = _wifiReady();
        if (wifiReady && !_sntpStarted &&
            (_lastSntpAttemptMs == 0 ||
             _elapsed(::platform::get_time(), _lastSntpAttemptMs,
                      _config.readinessLogIntervalMs))) {
            _lastSntpAttemptMs = ::platform::get_time();
            esp_sntp_config_t sntpConfig =
                ESP_NETIF_SNTP_DEFAULT_CONFIG(_config.sntpServer);
            const auto result = esp_netif_sntp_init(&sntpConfig);
            if (result == ESP_OK) {
                _sntpStarted = true;
                _sntpStartedMs = ::platform::get_time();
                _log_i("Assistant SNTP started: server=%s", _config.sntpServer);
            } else {
                _log_w("Assistant SNTP start failed: %s",
                       esp_err_to_name(result));
            }
        }

        if (_sntpStarted && !_sntpSynced &&
            esp_netif_sntp_sync_wait(0) == ESP_OK) {
            _sntpSynced = std::time(nullptr) >= _config.minimumValidEpoch;
            if (_sntpSynced) {
                _log_i("Assistant SNTP synchronized");
            } else {
                _log_w("Assistant SNTP returned an implausible clock");
            }
        }
        if (_sntpStarted && !_sntpSynced &&
            _elapsed(::platform::get_time(), _sntpStartedMs,
                     _config.sntpSyncTimeoutMs) &&
            !_sntpTimeoutLogged) {
            _sntpTimeoutLogged = true;
            _log_w("Assistant SNTP has not synchronized after %lums",
                   static_cast<unsigned long>(_config.sntpSyncTimeoutMs));
        }

        const auto ready = wifiReady && _clockReady();
        const auto nowMs = ::platform::get_time();
        if (!_readinessLogged || ready != _networkReady ||
            _elapsed(nowMs, _lastReadinessLogMs,
                     _config.readinessLogIntervalMs)) {
            _readinessLogged = true;
            _networkReady = ready;
            _lastReadinessLogMs = nowMs;
            _log_i("Assistant network readiness: wifi=%s clock=%s",
                   wifiReady ? "ready" : "waiting",
                   _clockReady() ? "ready" : "waiting");
        }
        _promoteListeningIfReady();
    }

    [[nodiscard]] bool _wifiReady() const {
        return _wifi != nullptr && _wifi->status().stationIpv4.valid;
    }

    [[nodiscard]] bool _clockReady() const {
        return _sntpSynced && std::time(nullptr) >= _config.minimumValidEpoch;
    }

    [[nodiscard]] int64_t _turnStartedEpochMs() const {
        timeval now{};
        gettimeofday(&now, nullptr);
        const auto nowEpochMs = static_cast<int64_t>(now.tv_sec) * 1000LL +
                                static_cast<int64_t>(now.tv_usec) / 1000LL;
        const auto elapsedMs =
            static_cast<uint32_t>(::platform::get_time() - _turnStartedMs);
        return nowEpochMs - static_cast<int64_t>(elapsedMs);
    }

    bool _waitForCaptureWriters() const {
        const auto startedMs = ::platform::get_time();
        while (_captureWriters.load(std::memory_order_acquire) != 0) {
            if (_elapsed(::platform::get_time(), startedMs,
                         _config.writerQuiesceTimeoutMs)) {
                return false;
            }
            ::platform::delay(::platform::ms_to_ticks(1));
        }
        return true;
    }

    void _discardCapture() {
        if (_captureRing == nullptr) {
            return;
        }
        for (;;) {
            std::size_t bytes = 0;
            auto *item = xRingbufferReceiveUpTo(_captureRing, &bytes, 0,
                                                _config.captureCapacityBytes);
            if (item == nullptr) {
                return;
            }
            vRingbufferReturnItem(_captureRing, item);
        }
    }

    [[nodiscard]] std::size_t _playbackQueueDepth() const {
        if (_playbackRing == nullptr) {
            return 0;
        }
        const auto freeBytes = xRingbufferGetCurFreeSize(_playbackRing);
        return freeBytes < _config.playbackQueueCapacityBytes
                   ? _config.playbackQueueCapacityBytes - freeBytes
                   : 0;
    }

    std::size_t _updatePlaybackQueueMetrics() {
        const auto depth = _playbackQueueDepth();
        _metrics->setPlaybackQueueDepth(depth);
        auto highWater =
            _playbackQueueHighWater.load(std::memory_order_relaxed);
        while (depth > highWater &&
               !_playbackQueueHighWater.compare_exchange_weak(
                   highWater, depth, std::memory_order_relaxed)) {
        }
        _metrics->setPlaybackQueueHighWater(
            _playbackQueueHighWater.load(std::memory_order_relaxed));
        return depth;
    }

    static void _updateMaximum(std::atomic<uint32_t> &maximum, uint32_t value) {
        auto current = maximum.load(std::memory_order_relaxed);
        while (value > current &&
               !maximum.compare_exchange_weak(current, value,
                                              std::memory_order_relaxed)) {
        }
    }

    void _recordPlaybackWait(uint32_t waitMs) {
        _updateMaximum(_playbackWaitMaxMs, waitMs);
        _metrics->setPlaybackWaitDuration(
            _playbackWaitMaxMs.load(std::memory_order_relaxed));
    }

    bool _waitForPlaybackWorker(uint32_t timeoutMs) {
        const auto startedMs = ::platform::get_time();
        while (!_playbackWorkerComplete.load(std::memory_order_acquire)) {
            if (_elapsed(::platform::get_time(), startedMs, timeoutMs)) {
                return false;
            }
            (void)ulTaskNotifyTake(
                pdTRUE, ::platform::ms_to_ticks(_config.playbackQueuePollMs));
        }
        return true;
    }

    void _discardPlayback() {
        if (_playbackRing == nullptr) {
            return;
        }
        for (;;) {
            std::size_t bytes = 0;
            auto *item = xRingbufferReceiveUpTo(_playbackRing, &bytes, 0,
                                                _config.playbackWriteBytes);
            if (item == nullptr) {
                _metrics->setPlaybackQueueDepth(0);
                return;
            }
            vRingbufferReturnItem(_playbackRing, item);
        }
    }

    void _stopPlayback() {
        _playbackProducerDone.store(true, std::memory_order_release);
        _playbackPrimeWritesEnabled.store(false, std::memory_order_release);
        if (!_playbackWorkerComplete.load(std::memory_order_acquire)) {
            _playbackAbort.store(true, std::memory_order_release);
            if (_playbackTask != nullptr) {
                xTaskNotifyGive(_playbackTask);
            }
            if (!_waitForPlaybackWorker(_config.playbackCompletionTimeoutMs)) {
                REPORT_IF_ERR(
                    ERR(CoreError, Timeout),
                    "Assistant playback worker did not stop during cleanup");
                return;
            }
        }
        _discardPlayback();
        _playbackAudioReady.store(false, std::memory_order_release);
        _playbackStarted = false;
        _playbackPrimingStarted = false;
    }

    void _releaseBuffers() {
        if (_captureRing != nullptr) {
            vRingbufferDeleteWithCaps(_captureRing);
            _captureRing = nullptr;
        }
        if (_playbackRing != nullptr) {
            vRingbufferDeleteWithCaps(_playbackRing);
            _playbackRing = nullptr;
        }
        heap_caps_free(_uploadBuffer);
        _uploadBuffer = nullptr;
        heap_caps_free(_playbackSilence);
        _playbackSilence = nullptr;
        _webSocket.end();
    }

    void _setState(AssistantTurnState value) {
        _state.store(value, std::memory_order_release);
        if (_metrics != nullptr) {
            _metrics->setState(value);
        }
    }

    void _promoteListeningIfReady() {
        if (_networkReady && _audioReady.load(std::memory_order_acquire) &&
            state() == AssistantTurnState::WaitingForWake) {
            _setInformationalStatus(AssistantInformationalStatus::Listening);
        }
    }

    void _setInformationalStatus(AssistantInformationalStatus value) {
        const auto current =
            _informationalStatus.load(std::memory_order_acquire);
        if (value == current) {
            return;
        }

        Totem::StatusLed::StateHandle *status = nullptr;
        switch (value) {
        case AssistantInformationalStatus::Listening:
            status = &_listeningStatus;
            break;
        case AssistantInformationalStatus::Playback:
            status = &_playbackStatus;
            break;
        case AssistantInformationalStatus::CoreReady:
            return;
        case AssistantInformationalStatus::Off: {
            const auto result = StatusLedService::setOff();
            if (!result.ok()) {
                _metrics->addStatusFailure();
                _log_w("Failed to set Off status: " ERR_FMT, ERR_ARG(result));
                return;
            }
            const auto previous = current;
            _informationalStatus.store(value, std::memory_order_release);
            _log_i("Assistant status changed: " SV_FMT " -> " SV_FMT,
                   MAGIC_SV_ARG(previous), MAGIC_SV_ARG(value));
            return;
        }
        }

        const auto result = status->set();
        if (!result.ok()) {
            _metrics->addStatusFailure();
            _log_w("Failed to set " SV_FMT " status: " ERR_FMT,
                   MAGIC_SV_ARG(value), ERR_ARG(result));
            return;
        }

        const auto previous = current;
        _informationalStatus.store(value, std::memory_order_release);
        _log_i("Assistant status changed: " SV_FMT " -> " SV_FMT,
               MAGIC_SV_ARG(previous), MAGIC_SV_ARG(value));
    }

    void _clearListeningStatus() {
        _setInformationalStatus(AssistantInformationalStatus::Off);
    }

    void _setRecordingStatus(bool active) {
        const auto previous =
            _recordingStatusActive.exchange(active, std::memory_order_acq_rel);
        if (previous == active) {
            return;
        }

        const auto result = _recordingStatus.set(active);
        if (!result.ok()) {
            _recordingStatusActive.store(previous, std::memory_order_release);
            _metrics->addStatusFailure();
            _log_w("Failed to set Recording status active=%s: " ERR_FMT,
                   active ? "true" : "false", ERR_ARG(result));
            return;
        }
        _log_i("Assistant status changed: Recording=%s",
               active ? "active" : "inactive");
    }

    static constexpr bool _commitEligible(CaptureEndReason reason) {
        return reason == CaptureEndReason::VadSilence ||
               reason == CaptureEndReason::MaximumDuration;
    }

    static constexpr TurnAbortReason _abortReason(CaptureEndReason reason) {
        switch (reason) {
        case CaptureEndReason::NoSpeechTimeout:
            return TurnAbortReason::NoSpeech;
        case CaptureEndReason::CaptureOverflow:
            return TurnAbortReason::CaptureOverflow;
        case CaptureEndReason::NetworkUnavailable:
            return TurnAbortReason::NetworkUnavailable;
        case CaptureEndReason::ClockUnavailable:
            return TurnAbortReason::ClockUnavailable;
        case CaptureEndReason::TransportFailure:
            return TurnAbortReason::Transport;
        case CaptureEndReason::PipelineStop:
            return TurnAbortReason::PipelineStop;
        default:
            return TurnAbortReason::Protocol;
        }
    }

    static constexpr bool _elapsed(uint32_t nowMs, uint32_t startedMs,
                                   uint32_t durationMs) {
        return static_cast<uint32_t>(nowMs - startedMs) >= durationMs;
    }

    static ReturnCode
    _consumeFrame(void *owner,
                  const Totem::AudioAfe::ProcessedFrameView &frame) {
        auto *self = static_cast<AssistantSession *>(owner);
        FAIL_IF_NULL(self, ERR(CoreError, InvalidArgument),
                     "Assistant session frame owner is null");
        return self->_consume(frame);
    }

    static ReturnCode _pipelineStopped(void *owner) {
        auto *self = static_cast<AssistantSession *>(owner);
        FAIL_IF_NULL(self, ERR(CoreError, InvalidArgument),
                     "Assistant session stop owner is null");
        return self->_stopPipeline();
    }

    static void _taskEntry(void *owner) {
        auto *self = static_cast<AssistantSession *>(owner);
        if (self == nullptr) {
            REPORT_IF_ERR(ERR(CoreError, InvalidArgument),
                          "Assistant network task owner is null");
            vTaskDeleteWithCaps(nullptr);
            return;
        }
        self->_runTask();
    }

    static void _playbackTaskEntry(void *owner) {
        auto *self = static_cast<AssistantSession *>(owner);
        if (self == nullptr) {
            REPORT_IF_ERR(ERR(CoreError, InvalidArgument),
                          "Assistant playback task owner is null");
            vTaskDeleteWithCaps(nullptr);
            return;
        }
        self->_runPlaybackTask();
    }

    AssistantSessionConfig _config{};
    AssistantWakeProfile _activeProfile{};
    Totem::Wifi::Wifi *_wifi = nullptr;
    Totem::AudioSink::I2SSink *_sink = nullptr;
    detail::AssistantMetrics *_metrics = nullptr;
    AssistantWebSocket _webSocket{};
    Pcm16PlaybackResampler _playbackResampler{};
    RingbufHandle_t _captureRing = nullptr;
    RingbufHandle_t _playbackRing = nullptr;
    TaskHandle_t _task = nullptr;
    TaskHandle_t _playbackTask = nullptr;
    std::byte *_uploadBuffer = nullptr;
    std::byte *_playbackSilence = nullptr;
    Totem::StatusLed::StateHandle _listeningStatus{};
    Totem::StatusLed::StateHandle _recordingStatus{};
    Totem::StatusLed::StateHandle _playbackStatus{};
    std::atomic<AssistantTurnState> _state{AssistantTurnState::WaitingForWake};
    std::atomic<CaptureEndReason> _captureEnd{CaptureEndReason::None};
    std::atomic<uint32_t> _captureWriters{0};
    std::atomic<uint32_t> _captureBytes{0};
    std::atomic<std::size_t> _captureHighWater{0};
    std::atomic<bool> _acceptingPcm{false};
    std::atomic<uint32_t> _captureEndedMs{0};
    std::atomic<bool> _audioReady{false};
    std::atomic<bool> _recordingStatusActive{false};
    std::atomic<bool> _active{false};
    std::atomic<bool> _playbackProducerDone{false};
    std::atomic<bool> _playbackAbort{false};
    std::atomic<bool> _playbackFailed{false};
    std::atomic<bool> _playbackWorkerComplete{true};
    std::atomic<bool> _playbackAudioReady{false};
    std::atomic<bool> _playbackPrimeWritesEnabled{false};
    std::atomic<bool> _playbackFirstWritePending{true};
    std::atomic<uint32_t> _audioAnnouncedMs{0};
    std::atomic<std::size_t> _playedBytes{0};
    std::atomic<std::size_t> _playbackQueueHighWater{0};
    std::atomic<uint32_t> _playbackWaitMaxMs{0};
    std::atomic<uint32_t> _playbackWriteMaxMs{0};
    std::atomic<uint32_t> _playbackUnderflows{0};
    std::atomic<AssistantInformationalStatus> _informationalStatus{
        AssistantInformationalStatus::CoreReady};
    Totem::AudioAfe::VadState _lastVad = Totem::AudioAfe::VadState::Silence;
    TurnAbortReason _turnAbortReason = TurnAbortReason::None;
    uint32_t _turnStartedMs = 0;
    uint32_t _commitMs = 0;
    uint32_t _lastSntpAttemptMs = 0;
    uint32_t _sntpStartedMs = 0;
    uint32_t _lastReadinessLogMs = 0;
    uint32_t _playbackPrimeStartedMs = 0;
    uint32_t _playbackStartedMs = 0;
    uint32_t _responseSampleRate = 0;
    uint32_t _playbackBoundaryInputMagnitude = 0;
    std::size_t _uploadSize = 0;
    std::size_t _sentBytes = 0;
    std::size_t _responseBytes = 0;
    std::size_t _playbackBoundaryMutedSamples = 0;
    uint32_t _uploadAppendCount = 0;
    uint32_t _uploadSendTotalMs = 0;
    uint32_t _uploadSendMaxMs = 0;
    bool _speechObserved = false;
    bool _hasLastVad = false;
    bool _turnCommitted = false;
    bool _turnCompleted = false;
    bool _turnEmptySample = false;
    bool _sntpStarted = false;
    bool _sntpSynced = false;
    bool _sntpTimeoutLogged = false;
    bool _networkReady = false;
    bool _readinessLogged = false;
    bool _playbackPrimingStarted = false;
    bool _playbackStarted = false;
    bool _playbackBoundaryPending = false;
    bool _playbackBoundaryInputObserved = false;

    static constexpr LogComponent logComponent = LogComponent::Audio;
};

} // namespace AiAudio
