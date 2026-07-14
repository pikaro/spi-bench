#pragma once

#include "AudioAfe/Interfaces/Config.hpp"
#include "AudioAfe/Interfaces/Types.hpp"
#include "AudioAfe/detail/Metrics.hpp"
#include "AudioAfe/detail/PlatformSelect.hpp"
#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "StaticConfig/AudioAfe.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "TaskController/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include "esp_err.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Totem::AudioAfe::detail {

class AfeProcessor : public HasLifecycle<AfeProcessor, Config>,
                     public HasTaskController<AfeProcessor, Config> {
    friend class HasLifecycle<AfeProcessor, Config>;
    friend struct LifecycleContract<AfeProcessor, Config>;
    friend TaskController::TaskHooks;
    friend class HasTaskController<AfeProcessor, Config>;
    friend struct TaskControllerContract<AfeProcessor>;
    friend struct TaskController::TaskHooks::Contract<AfeProcessor>;

  public:
    explicit AfeProcessor(TaskController::IRegistry &registry)
        : HasTaskController<AfeProcessor, Config>(registry) {}

    DELETE_COPY(AfeProcessor)
    DELETE_MOVE(AfeProcessor)

    static constexpr const char *name = "AudioAfe::AfeProcessor";
    static constexpr LogComponent logComponent = LogComponent::Audio;

    ReturnCode bind(InputBinding input, FrameSinkBinding sink) {
        FAIL_IF_ACTIVE(ERR(CoreError, InvalidState),
                       "Cannot bind AudioAfe while active");
        FAIL_IF(!input.valid() || !sink.valid(),
                ERR(CoreError, InvalidArgument),
                "Invalid AudioAfe input or frame sink binding");
        _input = input;
        _sink = sink;
        return OK();
    }

  private:
    ReturnCode _onBegin() {
        FAIL_IF(!_input.valid() || !_sink.valid(), ERR(CoreError, InvalidState),
                "AudioAfe bindings must be installed before begin");

        prewarmMetrics();
        _metrics = &metrics();
        _feedBytesFilled = 0;
        _inputSamplesAwaitingFetch = 0;
        _lastVad.reset();

        auto ret = _platform.begin(config());
        if (!ret.ok()) {
            _metrics->addFailure();
            _metrics = nullptr;
            return ret;
        }
        _feedFrameBytes = _platform.feedSamples() * sizeof(int16_t);

        ret = _startPumpTask();
        if (!ret.ok()) {
            _metrics->addFailure();
            (void)_platform.end();
            _metrics = nullptr;
            return ret;
        }

        _log_i("Audio AFE pump ready: feed=%uB fetch=%u samples task=%s",
               static_cast<unsigned>(_feedFrameBytes),
               static_cast<unsigned>(_platform.fetchSamples()),
               config().task.name);
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        if (_task != 0) {
            ret.combine(this->_endTaskController());
            _task = 0;
        }
        ret.combine(_platform.end());
        _feedBytesFilled = 0;
        _feedFrameBytes = 0;
        _inputSamplesAwaitingFetch = 0;
        _lastVad.reset();
        _metrics = nullptr;
        return ret;
    }

    ReturnCode _startPumpTask() {
        DEFAULT_TASK();
        _task = task;
        START_TASK();
        return OK();
    }

    ReturnCode _onTaskStart() {
        _feedBytesFilled = 0;
        _inputSamplesAwaitingFetch = 0;
        _lastVad.reset();
        return OK();
    }

    ReturnCode _onTaskStop() {
        _feedBytesFilled = 0;
        _inputSamplesAwaitingFetch = 0;
        auto ret = _sink.stop();
        if (!ret.ok() && _metrics != nullptr) {
            _metrics->addFailure();
        }
        return ret;
    }

    ReturnCode _onTaskStep() {
        auto *bytes = reinterpret_cast<uint8_t *>(_feedFrame.data());
        const auto requested = _feedFrameBytes - _feedBytesFilled;
        const auto bytesRead = _input.read(bytes + _feedBytesFilled, requested);
        if (_metrics != nullptr) {
            _metrics->recordSourceRead(requested, bytesRead);
        }
        FAIL_IF(bytesRead > requested, ERR(CoreError, Overflow),
                "AudioAfe source returned more bytes than requested");
        if (bytesRead == 0) {
            return OK();
        }

        _feedBytesFilled += bytesRead;
        if (_feedBytesFilled < _feedFrameBytes) {
            return OK();
        }

        auto feedRet = _platform.feed(_feedFrame.data());
        _feedBytesFilled = 0;
        if (!feedRet.ok()) {
            if (_metrics != nullptr) {
                _metrics->addFailure();
            }
            FAIL_ERR_FWD(feedRet, "Failed to feed ESP-SR AFE");
        }
        if (_metrics != nullptr) {
            _metrics->addFeed();
        }
        _inputSamplesAwaitingFetch += _platform.feedSamples();

        FAIL_IF_ERR_FWD(_drainFetchResults(),
                        "Failed to fetch ESP-SR output after feed");
        return OK();
    }

    ReturnCode _drainFetchResults() {
        for (uint8_t i = 0; i < config().maximumFetchesPerStep; ++i) {
            if (_inputSamplesAwaitingFetch < _platform.fetchSamples()) {
                return OK();
            }
            auto *result =
                _platform.fetch(::platform::ms_to_ticks(config().fetchWaitMs));
            if (result == nullptr || result->ret_value == ESP_ERR_TIMEOUT ||
                result->ret_value == ESP_FAIL) {
                if (_metrics != nullptr) {
                    _metrics->addFetchMiss();
                }
                return OK();
            }
            if (result->ret_value != ESP_OK) {
                if (_metrics != nullptr) {
                    _metrics->addFailure();
                }
                FAIL_IF(true, ERR(CoreError, OperationFailed),
                        "ESP-SR fetch returned 0x%x", result->ret_value);
            }
            FAIL_IF(result->data == nullptr || result->data_size <= 0 ||
                        (result->data_size % sizeof(int16_t)) != 0,
                    ERR(CoreError, InvalidData),
                    "ESP-SR fetch returned invalid PCM data");

            const auto sampleCount =
                static_cast<std::size_t>(result->data_size) / sizeof(int16_t);
            FAIL_IF(sampleCount > config().maximumFetchSamples,
                    ERR(CoreError, Overflow),
                    "ESP-SR fetch frame exceeds configured capacity");
            _inputSamplesAwaitingFetch -=
                std::min(_inputSamplesAwaitingFetch, sampleCount);

            const auto vad = result->vad_state == VAD_SPEECH
                                 ? VadState::Speech
                                 : VadState::Silence;
            const bool wakeDetected = result->wakeup_state == WAKENET_DETECTED;
            const auto samples =
                std::span<const int16_t>{result->data, sampleCount};

            if (_metrics != nullptr) {
                _metrics->addFetch();
                _metrics->recordAfe(result->data_volume,
                                    result->ringbuff_free_pct);
                _metrics->recordSignal(samples);
                if (wakeDetected) {
                    _metrics->addWake();
                }
                if (!_lastVad.has_value() || *_lastVad != vad) {
                    _metrics->addVad(vad);
                }
            }

            if (wakeDetected) {
                _log_i("WakeNet detected: model=%d word=%d length=%d "
                       "volume=%.1fdB",
                       result->wakenet_model_index, result->wake_word_index,
                       result->wake_word_length,
                       static_cast<double>(result->data_volume));
            }
            if (!_lastVad.has_value() || *_lastVad != vad) {
                _log_d("Raw VAD state: %s",
                       vad == VadState::Speech ? "speech" : "silence");
                _lastVad = vad;
            }

            const ProcessedFrameView frame{
                .samples = samples,
                .vad = vad,
                .wakeDetected = wakeDetected,
                .wakeWordIndex = result->wake_word_index,
                .wakeNetModelIndex = result->wakenet_model_index,
                .wakeWordLengthSamples = result->wake_word_length,
                .inputVolumeDb = result->data_volume,
                .ringBufferFreeFraction = result->ringbuff_free_pct,
                .timestampMs = ::platform::get_time(),
            };
            auto sinkRet = _sink.consume(frame);
            if (!sinkRet.ok()) {
                if (_metrics != nullptr) {
                    _metrics->addFailure();
                }
                FAIL_ERR_FWD(sinkRet, "AudioAfe processed-frame sink failed");
            }
        }
        return OK();
    }

    InputBinding _input{};
    FrameSinkBinding _sink{};
    Platform _platform{};
    Metrics *_metrics = nullptr;
    TaskController::RunnerKey _task = 0;
    std::array<int16_t, StaticConfig::AudioAfe::maxFeedSamples> _feedFrame{};
    std::size_t _feedFrameBytes = 0;
    std::size_t _feedBytesFilled = 0;
    std::size_t _inputSamplesAwaitingFetch = 0;
    std::optional<VadState> _lastVad{};
};

inline constexpr LifecycleContract<AfeProcessor, Config>
    _audio_afe_lifecycle_contract;
inline constexpr TaskControllerContract<AfeProcessor>
    _audio_afe_task_controller_contract;
inline constexpr TaskController::TaskHooks::Contract<AfeProcessor>
    _audio_afe_task_hooks_contract;

} // namespace Totem::AudioAfe::detail
