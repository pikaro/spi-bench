#pragma once

#include "Audio/Interfaces/Types.hpp"
#include "Audio/detail/BeatTracker.hpp"
#include "Audio/detail/I2SSource.hpp"
#include "Audio/detail/MagnitudeCache.hpp"
#include "Audio/detail/PlatformSelect.hpp"
#include "Audio/detail/Types.hpp"
#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "TaskController/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Totem::Audio::detail {

class FftAnalyzer : public HasLifecycle<FftAnalyzer, FftAnalyzerConfig>,
                    public HasTaskController<FftAnalyzer, FftAnalyzerConfig> {
    friend class HasLifecycle<FftAnalyzer, FftAnalyzerConfig>;
    friend struct LifecycleContract<FftAnalyzer, FftAnalyzerConfig>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<FftAnalyzer, FftAnalyzerConfig>;
    friend struct TaskControllerContract<FftAnalyzer>;
    friend struct TaskController::TaskHooks::Contract<FftAnalyzer>;

  public:
    DELETE_COPY(FftAnalyzer)
    DELETE_MOVE(FftAnalyzer)

    static constexpr const char *name = "Audio::FftAnalyzer";
    static constexpr LogComponent logComponent =
        Totem::Audio::detail::logComponent;

    FftAnalyzer(TaskController::IRegistry &registry, I2SSource &source)
        : HasTaskController<FftAnalyzer, FftAnalyzerConfig>(registry),
          _source(source) {}

    ReturnCode addFrameHandler(FftFrameHandler handler) {
        FAIL_IF_ACTIVE(ERR(CoreError, InvalidState),
                       "Cannot add FFT frame handler while analyzer is active");
        FAIL_IF(!handler.valid(), ERR(CoreError, InvalidArgument),
                "Invalid FFT frame handler");
        for (auto &slot : _frameHandlers) {
            if (!slot.valid()) {
                slot = handler;
                return OK();
            }
        }
        return ERR(CoreError, Overflow);
    }

    ReturnCode addBeatHandler(BeatHandler handler) {
        FAIL_IF_ACTIVE(ERR(CoreError, InvalidState),
                       "Cannot add beat handler while analyzer is active");
        FAIL_IF(!handler.valid(), ERR(CoreError, InvalidArgument),
                "Invalid beat handler");
        for (auto &slot : _beatHandlers) {
            if (!slot.valid()) {
                slot = handler;
                return OK();
            }
        }
        return ERR(CoreError, Overflow);
    }

    [[nodiscard]] FftRuntimeStats stats() const {
        return FftRuntimeStats{
            .copiedBytes = _copiedBytes.load(std::memory_order_acquire),
            .emptyCopies = _emptyCopies.load(std::memory_order_acquire),
            .frames = _frames.load(std::memory_order_acquire),
            .droppedFrames = _droppedFrames.load(std::memory_order_acquire),
            .beats = _beats.load(std::memory_order_acquire),
            .maxCopyUs = _maxCopyUs.load(std::memory_order_acquire),
            .maxFrameUs = _maxFrameUs.load(std::memory_order_acquire),
        };
    }

  private:
    struct BandPlan {
        FftBand band = FftBand::SubBass;
        uint16_t lowerHz = 0;
        uint16_t upperHz = 0;
        uint16_t lowerBin = 0;
        uint16_t upperBin = 0;
        float weighting = 1.0F;
    };

    ReturnCode _onBegin() {
        FAIL_IF(!_source.active(), ERR(CoreError, InvalidState),
                "Cannot begin FFT analyzer before I2S source is active");
        FAIL_IF(config().channel >= _source.audioInfo().channels,
                ERR(CoreError, InvalidArgument),
                "FFT channel %u is outside source channel count %u",
                config().channel, _source.audioInfo().channels);

        _buildBandPlan();
        _magnitudeCache.reset(config().magnitudeCache);
        _beatTracker.reset(config().beatTracker);
        _resetStats();

        _fftConfig = _fft.defaultConfig(audio_tools::TX_MODE);
        _fftConfig.sample_rate = _source.audioInfo().sampleRate;
        _fftConfig.channels = _source.audioInfo().channels;
        _fftConfig.bits_per_sample = _source.audioInfo().bitsPerSample;
        _fftConfig.channel_used = config().channel;
        _fftConfig.length = config().length;
        _fftConfig.stride = config().stride;
        _fftConfig.callback = _onFftResult;
        _fftConfig.ref = this;
        _fftConfig.window_function_fft = _windowFunction();

        FAIL_IF(!_fft.begin(_fftConfig), ERR(CoreError, OperationFailed),
                "Failed to begin audio-tools FFT sink");

        _copier.resize(config().copyBufferSizeBytes);
        _copier.setDelayOnNoData(1);
        _copier.setCheckAvailable(true);
        _copier.setCheckAvailableForWrite(false);
        _copier.setSynchAudioInfo(false);
        _copier.setLogName("AudioFft");
        _copier.begin(_fft, _source.input().stream());

        auto taskHooks = TaskController::TaskHooks::bind(*this);
        FAIL_IF_ERR_FWD(this->_beginTaskController(this->config().task),
                        "Failed to begin task controller for %s", name);
        auto taskAddResult =
            this->_taskController.addTask(this->config().task.name, taskHooks);
        FAIL_IF_UNEXPECTED(task, taskAddResult, taskAddResult.error(),
                           "Failed to bind task hooks for %s", name);
        _task = task;
        FAIL_IF_ERR_FWD(
            this->_taskController.startTask(_task, this->config().task),
            "Failed to start task for %s", name);

        _log_i("FFT analyzer started: %u samples, stride %u, %lu Hz",
               config().length, config().stride,
               static_cast<unsigned long>(_source.audioInfo().sampleRate));
        return OK();
    }

    ReturnCode _onEnd() {
        _task = 0;
        auto ret = this->_endTaskController();
        _copier.end();
        _fft.end();
        return ret;
    }

    ReturnCode _onTaskStep() {
        const auto startedUs = ::platform::get_time_us();
        const auto copied = _copier.copy();
        const auto elapsedUs =
            static_cast<uint32_t>(::platform::get_time_us() - startedUs);
        _recordMax(_maxCopyUs, elapsedUs);

        if (copied == 0) {
            _emptyCopies.fetch_add(1, std::memory_order_acq_rel);
        } else {
            _copiedBytes.fetch_add(static_cast<uint32_t>(copied),
                                   std::memory_order_acq_rel);
        }
        return OK();
    }

    void _buildBandPlan() {
        const auto sampleRate =
            static_cast<float>(_source.audioInfo().sampleRate);
        const auto maxBin = static_cast<uint16_t>((config().length / 2U) - 1U);
        const auto binWidth = sampleRate / static_cast<float>(config().length);
        uint16_t previousUpper = 0;

        for (size_t i = 0; i < fftBandCount; ++i) {
            const auto &band = config().bands[i];
            auto lower = static_cast<uint16_t>(
                std::floor(static_cast<float>(band.lowerHz) / binWidth));
            if (i == 0) {
                lower = std::max<uint16_t>(1, lower);
            } else {
                lower = static_cast<uint16_t>(
                    std::min<uint16_t>(maxBin, previousUpper + 1U));
            }

            auto upper = static_cast<uint16_t>(
                std::ceil(static_cast<float>(band.upperHz) / binWidth));
            upper = upper == 0 ? 0 : static_cast<uint16_t>(upper - 1U);
            upper = std::min<uint16_t>(upper, maxBin);
            if (upper < lower) {
                upper = lower;
            }

            const auto centerHz =
                (static_cast<float>(band.lowerHz) +
                 static_cast<float>(band.upperHz)) *
                0.5F;
            _bandPlan[i] = BandPlan{
                .band = static_cast<FftBand>(i),
                .lowerHz = band.lowerHz,
                .upperHz = band.upperHz,
                .lowerBin = lower,
                .upperBin = upper,
                .weighting = _weighting(centerHz) * config().bandGains[i],
            };
            previousUpper = upper;
        }
    }

    Platform::WindowFunction *_windowFunction() {
        switch (config().window) {
        case FftWindow::Hamming:
            return &_hamming;
        case FftWindow::Hann:
            return &_hann;
        case FftWindow::None:
        default:
            return nullptr;
        }
    }

    [[nodiscard]] float _weighting(float frequencyHz) const {
        if (config().weighting != FftWeighting::AWeighting) {
            return 1.0F;
        }
        if (frequencyHz <= 0.0F) {
            return 0.0F;
        }

        const auto f2 = static_cast<double>(frequencyHz) *
                        static_cast<double>(frequencyHz);
        const auto numerator = (12200.0 * 12200.0) * f2 * f2;
        const auto denominator =
            (f2 + (20.6 * 20.6)) *
            std::sqrt((f2 + (107.7 * 107.7)) *
                      (f2 + (737.9 * 737.9))) *
            (f2 + (12200.0 * 12200.0));
        const auto ratio = numerator / denominator;
        const auto db = 2.0 + (20.0 * std::log10(ratio));
        return static_cast<float>(std::pow(10.0, db / 20.0));
    }

    static void _onFftResult(Platform::AudioFftBase &fft) {
        auto *self = static_cast<FftAnalyzer *>(fft.config().ref);
        if (self == nullptr) {
            return;
        }
        self->_handleFftResult(fft);
    }

    void _handleFftResult(Platform::AudioFftBase &fft) {
        const auto startedUs = ::platform::get_time_us();
        if (_handlingFrame.exchange(true, std::memory_order_acq_rel)) {
            _droppedFrames.fetch_add(1, std::memory_order_acq_rel);
            return;
        }

        auto frame = FftFrame{
            .audio = _source.audioInfo(),
            .sequence = _nextFrameSequence++,
            .timestampUs = static_cast<uint64_t>(startedUs),
            .length = config().length,
            .stride = config().stride,
        };

        for (size_t i = 0; i < fftBandCount; ++i) {
            frame.bands[i] = _computeBand(fft, _bandPlan[i]);
        }

        _magnitudeCache.update(frame);
        _dispatchFrame(frame);

        if (auto beat = _beatTracker.update(frame); beat.has_value()) {
            _beats.fetch_add(1, std::memory_order_acq_rel);
            _dispatchBeat(*beat);
        }

        _frames.fetch_add(1, std::memory_order_acq_rel);
        const auto elapsedUs =
            static_cast<uint32_t>(::platform::get_time_us() - startedUs);
        _recordMax(_maxFrameUs, elapsedUs);
        _handlingFrame.store(false, std::memory_order_release);
    }

    FftBandValue _computeBand(Platform::AudioFftBase &fft,
                              const BandPlan &plan) const {
        float sum = 0.0F;
        float squareSum = 0.0F;
        uint16_t count = 0;
        for (uint16_t bin = plan.lowerBin; bin <= plan.upperBin; ++bin) {
            const auto magnitude = fft.magnitude(bin);
            sum += magnitude;
            squareSum += magnitude * magnitude;
            ++count;
        }
        if (count == 0) {
            count = 1;
        }

        float magnitude = sum;
        switch (config().magnitudeMode) {
        case FftMagnitudeMode::Average:
            magnitude = sum / static_cast<float>(count);
            break;
        case FftMagnitudeMode::Rms:
            magnitude = std::sqrt(squareSum / static_cast<float>(count));
            break;
        case FftMagnitudeMode::Sum:
        default:
            magnitude = sum;
            break;
        }

        return FftBandValue{
            .band = plan.band,
            .lowerHz = plan.lowerHz,
            .upperHz = plan.upperHz,
            .lowerBin = plan.lowerBin,
            .upperBin = plan.upperBin,
            .magnitude = magnitude,
            .weightedMagnitude = magnitude * plan.weighting,
        };
    }

    void _dispatchFrame(const FftFrame &frame) {
        for (const auto &handler : _frameHandlers) {
            if (!handler.valid()) {
                continue;
            }
            auto ret = handler.callback(handler.owner, frame);
            if (!ret.ok()) {
                _log_w("FFT frame handler failed: " ERR_FMT, ERR_ARG(ret));
            }
        }
    }

    void _dispatchBeat(const BeatEvent &event) {
        for (const auto &handler : _beatHandlers) {
            if (!handler.valid()) {
                continue;
            }
            auto ret = handler.callback(handler.owner, event);
            if (!ret.ok()) {
                _log_w("Beat handler failed: " ERR_FMT, ERR_ARG(ret));
            }
        }
    }

    static void _recordMax(std::atomic<uint32_t> &target, uint32_t value) {
        auto current = target.load(std::memory_order_acquire);
        while (value > current &&
               !target.compare_exchange_weak(current, value,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
        }
    }

    void _resetStats() {
        _copiedBytes.store(0, std::memory_order_release);
        _emptyCopies.store(0, std::memory_order_release);
        _frames.store(0, std::memory_order_release);
        _droppedFrames.store(0, std::memory_order_release);
        _beats.store(0, std::memory_order_release);
        _maxCopyUs.store(0, std::memory_order_release);
        _maxFrameUs.store(0, std::memory_order_release);
        _nextFrameSequence = 1;
        _handlingFrame.store(false, std::memory_order_release);
    }

    I2SSource &_source;
    Platform::FftSink _fft{};
    Platform::AudioFftConfig _fftConfig{};
    Platform::StreamCopier _copier{0};
    Platform::HammingWindow _hamming{};
    Platform::HannWindow _hann{};
    std::array<BandPlan, fftBandCount> _bandPlan{};
    MagnitudeCache _magnitudeCache{};
    BeatTracker _beatTracker{};
    std::array<FftFrameHandler, fftMaxFrameHandlers> _frameHandlers{};
    std::array<BeatHandler, fftMaxBeatHandlers> _beatHandlers{};
    Totem::TaskController::RunnerKey _task = 0;
    uint32_t _nextFrameSequence = 1;
    std::atomic<bool> _handlingFrame{false};
    std::atomic<uint32_t> _copiedBytes{0};
    std::atomic<uint32_t> _emptyCopies{0};
    std::atomic<uint32_t> _frames{0};
    std::atomic<uint32_t> _droppedFrames{0};
    std::atomic<uint32_t> _beats{0};
    std::atomic<uint32_t> _maxCopyUs{0};
    std::atomic<uint32_t> _maxFrameUs{0};
};

} // namespace Totem::Audio::detail
