#pragma once

#include "AudioAfe/Interfaces/Types.hpp"
#include "AudioSink/Facade.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include "Services/StatusLed.hpp"
#include "StaticConfig/AudioAfe.hpp"
#include "StatusLed/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace AiAudio {

struct DelayedPlaybackConfig {
    uint32_t sampleRate = 16000;
    uint32_t delayMs = 1000;

    [[nodiscard]] constexpr bool validate() const {
        const auto scaled = static_cast<uint64_t>(sampleRate) * delayMs;
        return sampleRate == 16000 && delayMs > 0 && scaled % 1000U == 0 &&
               scaled / 1000U <= 16000U;
    }

    [[nodiscard]] constexpr std::size_t delaySamples() const {
        return static_cast<std::size_t>(
            (static_cast<uint64_t>(sampleRate) * delayMs) / 1000U);
    }
};

struct WakeSessionConfig {
    uint32_t noSpeechTimeoutMs = 5000;
    uint32_t maximumSessionMs = 30000;
    Totem::StatusLed::StateDef recordingStatus{
        .name = "Recording",
        .color = {.red = 160, .green = 80, .blue = 0},
        .kind = Totem::StatusLed::StateKind::Warning,
    };

    [[nodiscard]] constexpr bool validate() const {
        return noSpeechTimeoutMs > 0 && maximumSessionMs > 0 &&
               noSpeechTimeoutMs < maximumSessionMs &&
               recordingStatus.validate();
    }
};

namespace detail {

enum class SessionCloseReason : uint8_t {
    VadSilence,
    NoSpeechTimeout,
    MaximumDuration,
    PipelineStop,
};

struct SessionMetrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using GaugeHandle = Totem::MetricsBackend::GaugeHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;
    using MetricUnit = Totem::MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc groupDef{
        .name = "aiSess",
        .level = Totem::MetricsBackend::MetricLevel::Baseline,
    };
    static constexpr MetricDesc openedDef{.name = "open",
                                          .type = MetricType::Counter};
    static constexpr MetricDesc vadClosedDef{.name = "endVad",
                                             .type = MetricType::Counter};
    static constexpr MetricDesc noSpeechDef{.name = "endNoSp",
                                            .type = MetricType::Counter};
    static constexpr MetricDesc maximumDef{.name = "endMax",
                                           .type = MetricType::Counter};
    static constexpr MetricDesc stoppedDef{.name = "endStop",
                                           .type = MetricType::Counter};
    static constexpr MetricDesc repeatedDef{.name = "repeat",
                                            .type = MetricType::Counter};
    static constexpr MetricDesc forwardedBytesDef{
        .name = "fwdB", .type = MetricType::Counter, .unit = MetricUnit::Bytes};
    static constexpr MetricDesc zeroBytesDef{.name = "zeroB",
                                             .type = MetricType::Counter,
                                             .unit = MetricUnit::Bytes};
    static constexpr MetricDesc playbackSamplesDef{.name = "playSamp",
                                                   .type = MetricType::Counter};
    static constexpr MetricDesc sinkDropsDef{.name = "sinkDrop",
                                             .type = MetricType::Counter};
    static constexpr MetricDesc statusFailuresDef{.name = "statFail",
                                                  .type = MetricType::Counter};
    static constexpr MetricDesc activeDef{.name = "active",
                                          .type = MetricType::Gauge};
    static constexpr MetricDesc durationDef{.name = "durMs",
                                            .type = MetricType::Gauge,
                                            .unit = MetricUnit::Milliseconds};

    static SessionMetrics create() {
        REGISTER_METRICS_GROUP("AiAudioSession", group);
        REGISTER_METRIC("AiAudioSession", opened, Counter, group);
        REGISTER_METRIC("AiAudioSession", vadClosed, Counter, group);
        REGISTER_METRIC("AiAudioSession", noSpeech, Counter, group);
        REGISTER_METRIC("AiAudioSession", maximum, Counter, group);
        REGISTER_METRIC("AiAudioSession", stopped, Counter, group);
        REGISTER_METRIC("AiAudioSession", repeated, Counter, group);
        REGISTER_METRIC("AiAudioSession", forwardedBytes, Counter, group);
        REGISTER_METRIC("AiAudioSession", zeroBytes, Counter, group);
        REGISTER_METRIC("AiAudioSession", playbackSamples, Counter, group);
        REGISTER_METRIC("AiAudioSession", sinkDrops, Counter, group);
        REGISTER_METRIC("AiAudioSession", statusFailures, Counter, group);
        REGISTER_METRIC("AiAudioSession", active, Gauge, group);
        REGISTER_METRIC("AiAudioSession", duration, Gauge, group);
        return SessionMetrics{.group = group,
                              .opened = opened,
                              .vadClosed = vadClosed,
                              .noSpeech = noSpeech,
                              .maximum = maximum,
                              .stopped = stopped,
                              .repeated = repeated,
                              .forwardedBytes = forwardedBytes,
                              .zeroBytes = zeroBytes,
                              .playbackSamples = playbackSamples,
                              .sinkDrops = sinkDrops,
                              .statusFailures = statusFailures,
                              .active = active,
                              .duration = duration};
    }

    void addOpened() const {
        METRIC_INCR(group, opened, 1);
        METRIC_SET(group, active, 1);
    }
    void addRepeated() const { METRIC_INCR(group, repeated, 1); }
    void addClosed(SessionCloseReason reason, uint32_t durationMs) const {
        switch (reason) {
        case SessionCloseReason::VadSilence:
            METRIC_INCR(group, vadClosed, 1);
            break;
        case SessionCloseReason::NoSpeechTimeout:
            METRIC_INCR(group, noSpeech, 1);
            break;
        case SessionCloseReason::MaximumDuration:
            METRIC_INCR(group, maximum, 1);
            break;
        case SessionCloseReason::PipelineStop:
            METRIC_INCR(group, stopped, 1);
            break;
        }
        METRIC_SET(group, duration, durationMs);
        METRIC_SET(group, active, 0);
    }
    void addRouting(std::size_t bytes, bool recording) const {
        if (recording) {
            METRIC_INCR(group, forwardedBytes, static_cast<uint32_t>(bytes));
        } else {
            METRIC_INCR(group, zeroBytes, static_cast<uint32_t>(bytes));
        }
        METRIC_INCR(group, playbackSamples,
                    static_cast<uint32_t>(bytes / sizeof(int16_t)));
    }
    void addSinkDrop() const { METRIC_INCR(group, sinkDrops, 1); }
    void addStatusFailure() const { METRIC_INCR(group, statusFailures, 1); }

    GroupHandle group;
    CounterHandle opened;
    CounterHandle vadClosed;
    CounterHandle noSpeech;
    CounterHandle maximum;
    CounterHandle stopped;
    CounterHandle repeated;
    CounterHandle forwardedBytes;
    CounterHandle zeroBytes;
    CounterHandle playbackSamples;
    CounterHandle sinkDrops;
    CounterHandle statusFailures;
    GaugeHandle active;
    GaugeHandle duration;

    static constexpr auto component =
        Totem::MetricsBackend::MetricComponent::Audio;
};

DEFINE_PREWARMED_METRICS_ACCESSORS(SessionMetrics, sessionMetrics,
                                   prewarmSessionMetrics)

class DelayedPlayback {
  public:
    ReturnCode begin(Totem::AudioSink::I2SSink &sink,
                     DelayedPlaybackConfig config) {
        FAIL_IF(_sink != nullptr, ERR(CoreError, InvalidState),
                "Delayed playback is already active");
        FAIL_IF_NOT(config.validate(), ERR(CoreError, InvalidArgument),
                    "Invalid delayed playback config");
        FAIL_IF(!sink.active(), ERR(CoreError, InvalidState),
                "Cannot begin delayed playback before I2S sink");
        _sink = &sink;
        _delaySamples = config.delaySamples();
        clear();
        return OK();
    }

    ReturnCode consume(std::span<const int16_t> samples, bool recording,
                       const SessionMetrics &metrics) {
        FAIL_IF(_sink == nullptr || _delaySamples == 0,
                ERR(CoreError, InvalidState), "Delayed playback is not active");
        FAIL_IF(samples.size() > _output.size(), ERR(CoreError, Overflow),
                "Delayed playback frame exceeds output capacity");

        for (std::size_t i = 0; i < samples.size(); ++i) {
            _output[i] = _delayLine[_delayIndex];
            _delayLine[_delayIndex] = recording ? samples[i] : int16_t{0};
            _delayIndex = (_delayIndex + 1U) % _delaySamples;
        }

        const auto bytes = samples.size_bytes();
        metrics.addRouting(bytes, recording);
        const auto writable = _sink->stream().availableForWrite();
        if (writable < 0 || static_cast<std::size_t>(writable) < bytes) {
            metrics.addSinkDrop();
            return OK();
        }

        const auto written = _sink->stream().write(
            reinterpret_cast<const uint8_t *>(_output.data()), bytes);
        if (written != bytes) {
            metrics.addSinkDrop();
        }
        return OK();
    }

    void clear() {
        _delayLine.fill(0);
        _output.fill(0);
        _delayIndex = 0;
    }

  private:
    static constexpr std::size_t maximumDelaySamples = 16000;
    Totem::AudioSink::I2SSink *_sink = nullptr;
    std::array<int16_t, maximumDelaySamples> _delayLine{};
    std::array<int16_t, Totem::StaticConfig::AudioAfe::maxFetchSamples>
        _output{};
    std::size_t _delaySamples = 0;
    std::size_t _delayIndex = 0;
};

} // namespace detail

class WakeSession {
  public:
    DELETE_COPY(WakeSession)
    DELETE_MOVE(WakeSession)

    WakeSession() = default;

    ReturnCode begin(Totem::AudioSink::I2SSink &sink, WakeSessionConfig config,
                     DelayedPlaybackConfig playbackConfig) {
        FAIL_IF(_active, ERR(CoreError, InvalidState),
                "Wake session is already active");
        FAIL_IF_NOT(config.validate() && playbackConfig.validate(),
                    ERR(CoreError, InvalidArgument),
                    "Invalid wake session config");

        detail::prewarmSessionMetrics();
        _metrics = &detail::sessionMetrics();
        FAIL_IF_ERR_FWD(_playback.begin(sink, playbackConfig),
                        "Failed to begin delayed playback");
        FAIL_IF_UNEXPECTED_FWD(
            recording,
            StatusLedService::directory().registerState(config.recordingStatus),
            "Failed to register Recording status LED state");
        _recordingStatus = recording;
        _config = config;
        _active = true;
        _log_i("Wake session ready: no-speech=%lums maximum=%lums "
               "debug-delay=%lums",
               static_cast<unsigned long>(_config.noSpeechTimeoutMs),
               static_cast<unsigned long>(_config.maximumSessionMs),
               static_cast<unsigned long>(playbackConfig.delayMs));
        return OK();
    }

    [[nodiscard]] Totem::AudioAfe::FrameSinkBinding binding() {
        return Totem::AudioAfe::FrameSinkBinding{
            .owner = this,
            .consumeFrame = WakeSession::_consumeFrame,
            .pipelineStopped = WakeSession::_pipelineStopped,
        };
    }

  private:
    enum class State : uint8_t { WaitingForWake, Recording };

    ReturnCode _consume(const Totem::AudioAfe::ProcessedFrameView &frame) {
        FAIL_IF(!_active, ERR(CoreError, InvalidState),
                "Wake session is not active");

        const bool wasRecording = _state == State::Recording;
        if (frame.wakeDetected) {
            if (!wasRecording) {
                FAIL_IF_ERR_FWD(_open(frame),
                                "Failed to open wake recording session");
            } else {
                _metrics->addRepeated();
                _log_i("Repeated WakeNet detection during active recording: "
                       "model=%d word=%d",
                       frame.wakeNetModelIndex, frame.wakeWordIndex);
            }
        }

        if (wasRecording) {
            auto endpoint = _updateEndpoint(frame);
            if (endpoint.has_value()) {
                FAIL_IF_ERR_FWD(_close(*endpoint, frame.timestampMs),
                                "Failed to close wake recording session");
            }
        }

        return _playback.consume(frame.samples, _state == State::Recording,
                                 *_metrics);
    }

    ReturnCode _open(const Totem::AudioAfe::ProcessedFrameView &frame) {
        auto statusRet = _recordingStatus.set();
        if (!statusRet.ok()) {
            _metrics->addStatusFailure();
            FAIL_ERR_FWD(statusRet, "Failed to set Recording status");
        }
        _state = State::Recording;
        _sessionStartedMs = frame.timestampMs;
        _speechObserved = false;
        _lastSessionVad.reset();
        _metrics->addOpened();
        _log_i("Recording started by WakeNet: model=%d word=%d "
               "volume=%.1fdB",
               frame.wakeNetModelIndex, frame.wakeWordIndex,
               static_cast<double>(frame.inputVolumeDb));
        return OK();
    }

    std::optional<detail::SessionCloseReason>
    _updateEndpoint(const Totem::AudioAfe::ProcessedFrameView &frame) {
        if (frame.vad == Totem::AudioAfe::VadState::Speech) {
            if (!_speechObserved) {
                _speechObserved = true;
                _log_i("Post-wake VAD detected command speech");
            }
        } else if (_speechObserved && _lastSessionVad.has_value() &&
                   *_lastSessionVad == Totem::AudioAfe::VadState::Speech) {
            _lastSessionVad = frame.vad;
            return detail::SessionCloseReason::VadSilence;
        }
        _lastSessionVad = frame.vad;

        if (!_speechObserved && _elapsed(frame.timestampMs, _sessionStartedMs,
                                         _config.noSpeechTimeoutMs)) {
            return detail::SessionCloseReason::NoSpeechTimeout;
        }
        if (_elapsed(frame.timestampMs, _sessionStartedMs,
                     _config.maximumSessionMs)) {
            return detail::SessionCloseReason::MaximumDuration;
        }
        return std::nullopt;
    }

    ReturnCode _close(detail::SessionCloseReason reason, uint32_t nowMs) {
        if (_state != State::Recording) {
            return OK();
        }
        const auto durationMs = nowMs - _sessionStartedMs;
        _state = State::WaitingForWake;
        _speechObserved = false;
        _lastSessionVad.reset();
        _metrics->addClosed(reason, durationMs);

        auto statusRet = _recordingStatus.reset();
        if (!statusRet.ok()) {
            _metrics->addStatusFailure();
            FAIL_ERR_FWD(statusRet, "Failed to reset Recording status");
        }
        _log_i("Recording stopped: reason=%s duration=%lums",
               _reasonName(reason), static_cast<unsigned long>(durationMs));
        return OK();
    }

    ReturnCode _stop() {
        if (!_active) {
            return OK();
        }
        auto ret = OK();
        if (_state == State::Recording) {
            ret.combine(_close(detail::SessionCloseReason::PipelineStop,
                               ::platform::get_time()));
        } else if (_recordingStatus.valid()) {
            auto statusRet = _recordingStatus.reset();
            if (!statusRet.ok()) {
                _metrics->addStatusFailure();
                ret.combine(statusRet);
            }
        }
        _playback.clear();
        return ret;
    }

    static ReturnCode
    _consumeFrame(void *owner,
                  const Totem::AudioAfe::ProcessedFrameView &frame) {
        auto *self = static_cast<WakeSession *>(owner);
        FAIL_IF_NULL(self, ERR(CoreError, InvalidArgument),
                     "Wake session frame owner is null");
        return self->_consume(frame);
    }

    static ReturnCode _pipelineStopped(void *owner) {
        auto *self = static_cast<WakeSession *>(owner);
        FAIL_IF_NULL(self, ERR(CoreError, InvalidArgument),
                     "Wake session stop owner is null");
        return self->_stop();
    }

    static constexpr bool _elapsed(uint32_t nowMs, uint32_t startedMs,
                                   uint32_t durationMs) {
        return static_cast<uint32_t>(nowMs - startedMs) >= durationMs;
    }

    static constexpr const char *
    _reasonName(detail::SessionCloseReason reason) {
        switch (reason) {
        case detail::SessionCloseReason::VadSilence:
            return "vad-silence";
        case detail::SessionCloseReason::NoSpeechTimeout:
            return "no-speech-timeout";
        case detail::SessionCloseReason::MaximumDuration:
            return "maximum-duration";
        case detail::SessionCloseReason::PipelineStop:
            return "pipeline-stop";
        }
        return "unknown";
    }

    WakeSessionConfig _config{};
    detail::DelayedPlayback _playback{};
    detail::SessionMetrics *_metrics = nullptr;
    Totem::StatusLed::StateHandle _recordingStatus{};
    State _state = State::WaitingForWake;
    uint32_t _sessionStartedMs = 0;
    bool _speechObserved = false;
    bool _active = false;
    std::optional<Totem::AudioAfe::VadState> _lastSessionVad{};

    static constexpr LogComponent logComponent = LogComponent::Audio;
};

} // namespace AiAudio
