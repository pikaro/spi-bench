#pragma once

#include "Audio/Interfaces/AnalyzerConfig.hpp"
#include "Audio/Interfaces/Types.hpp"
#include "Audio/Interfaces/Wire.hpp"
#include "Audio/detail/BeatTracker.hpp"
#include "Audio/detail/Commands.hpp"
#include "Audio/detail/FftBackend.hpp"
#include "Audio/detail/MagnitudeCache.hpp"
#include "Audio/detail/Metrics.hpp"
#include "Audio/detail/PlatformSelect.hpp"
#include "Audio/detail/Sources/IAudioSource.hpp"
#include "Audio/detail/Types.hpp"
#include "AudioTools/CoreAudio/AudioTypes.h"
#include "Base/HasCommands.hpp"
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
                    public HasTaskController<FftAnalyzer, FftAnalyzerConfig>,
                    public HasCommands<FftAnalyzer, Commands<FftAnalyzer>> {
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

    FftAnalyzer(TaskController::IRegistry &registry, IAudioSource &source)
        : HasTaskController<FftAnalyzer, FftAnalyzerConfig>(registry),
          _source(source) {}

    ReturnCode addFrameHandler(FftResultHandler handler) {
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

    ReturnCode addBeatHandler(BeatResultHandler handler) {
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
            .copyCalls = _copyCalls.load(std::memory_order_acquire),
            .copiedBytes = _copiedBytes.load(std::memory_order_acquire),
            .emptyCopies = _emptyCopies.load(std::memory_order_acquire),
            .readinessProbes = _readinessProbes.load(std::memory_order_acquire),
            .sourceUnavailableSkips =
                _sourceUnavailableSkips.load(std::memory_order_acquire),
            .frames = _frames.load(std::memory_order_acquire),
            .droppedFrames = _droppedFrames.load(std::memory_order_acquire),
            .beats = _beats.load(std::memory_order_acquire),
            .maxCopyUs = _maxCopyUs.load(std::memory_order_acquire),
            .maxReadinessProbeUs =
                _maxReadinessProbeUs.load(std::memory_order_acquire),
            .maxFrameUs = _maxFrameUs.load(std::memory_order_acquire),
            .maxBandComputeUs =
                _maxBandComputeUs.load(std::memory_order_acquire),
            .maxMagnitudeCacheUs =
                _maxMagnitudeCacheUs.load(std::memory_order_acquire),
            .maxFrameDispatchUs =
                _maxFrameDispatchUs.load(std::memory_order_acquire),
            .maxBeatUpdateUs = _maxBeatUpdateUs.load(std::memory_order_acquire),
            .maxBeatDispatchUs =
                _maxBeatDispatchUs.load(std::memory_order_acquire),
        };
    }

    [[nodiscard]] BeatTrackerStatus beatStatus() const {
        const auto nowMs = ::platform::get_time();
        auto status = BeatTrackerStatus{
            .primaryGroup = config().beatTracker.primaryGroup,
        };
        for (size_t i = 0; i < beatGroupCount; ++i) {
            status.groups[i] = _groupBeatStatus(i, nowMs);
        }
        status.primary = status.groups[beatGroupIndex(status.primaryGroup)];
        return status;
    }

  private:
    struct BandPlan {
        FftBand band = FftBand::SubBass;
        uint16_t lowerHz = 0;
        uint16_t upperHz = 0;
        uint16_t lowerBin = 0;
        uint16_t upperBin = 0;
        float preparationGain = 1.0F;
    };

    ReturnCode _onBegin() {
        FAIL_IF(!_source.active(), ERR(CoreError, InvalidState),
                "Cannot begin FFT analyzer before audio source is active");
        FAIL_IF(config().channel >= _source.audioInfo().channels,
                ERR(CoreError, InvalidArgument),
                "FFT channel %u is outside source channel count %u",
                config().channel, _source.audioInfo().channels);

        _buildBandPlan();
        _magnitudeCache.reset(config().magnitudeCache);
        _beatTracker.reset(config().beatTracker);
        _resetStats();
        prewarmMetrics();
        _metrics = &metrics();

        FAIL_IF_ERR_FWD(_fftBackend.begin(config(), _source.audioInfo(),
                                          _windowFunction(), _onFftResult,
                                          this),
                        "Failed to begin FFT backend");

        _copier.resize(config().copyBufferSizeBytes);
        _copier.setDelayOnNoData(0);
        _copier.setCheckAvailable(true);
        _copier.setCheckAvailableForWrite(false);
        _copier.setSynchAudioInfo(false);
        _copier.setLogName("AudioFft");
        _copier.begin(_fftBackend.stream(), _source.stream());

        FAIL_IF_ERR_FWD(_registerCommands(),
                        "Failed to register FFT analyzer commands");

        DEFAULT_TASK();
        _task = task;
        START_TASK();

        _log_i("FFT analyzer started: source=%s, backend=%s, %u samples, "
               "stride %u, %lu Hz",
               _source.sourceName(), FftBackend::backendName(config().backend),
               config().length, config().stride,
               static_cast<unsigned long>(_source.audioInfo().sampleRate));
        return OK();
    }

    ReturnCode _onEnd() {
        _task = 0;
        auto ret = this->_endTaskController();
        _copier.end();
        ret.combine(_fftBackend.end());
        ret.combine(_deregisterCommands());
        _metrics = nullptr;
        return ret;
    }

    ReturnCode _onTaskStep() {
        const auto startedUs = _profilingTimestampUs();
        const auto nowMs = ::platform::get_time();
        _copyCalls.fetch_add(1, std::memory_order_acq_rel);
        if (_metrics != nullptr) {
            _metrics->addTaskStep();
        }

        if (!_source.ready()) {
            _sourceUnavailableSkips.fetch_add(1, std::memory_order_acq_rel);
            if (_metrics != nullptr) {
                _metrics->addSourceUnavailableSkip();
            }
            const auto probesBefore = _source.readinessProbeCount();
            const auto probeStartedUs = _profilingTimestampUs();
            (void)_source.pollReadiness(nowMs);
            const auto probeElapsedUs = _profilingElapsedUs(probeStartedUs);
            if (_source.readinessProbeCount() != probesBefore) {
                _readinessProbes.fetch_add(1, std::memory_order_acq_rel);
                _recordMax(_maxReadinessProbeUs, probeElapsedUs);
                if (_metrics != nullptr) {
                    _metrics->addReadinessProbe(probeElapsedUs);
                }
            }
            const auto elapsedUs = _profilingElapsedUs(startedUs);
            _recordMax(_maxCopyUs, elapsedUs);
            if (_metrics != nullptr) {
                _metrics->recordCopyDuration(elapsedUs);
            }
            return OK();
        }

        const auto copied = _copier.copy();
        _source.observeReadResult(copied, nowMs);
        const auto elapsedUs = _profilingElapsedUs(startedUs);
        _recordMax(_maxCopyUs, elapsedUs);
        if (_metrics != nullptr) {
            _metrics->addCopyResult(static_cast<uint32_t>(copied), elapsedUs);
        }

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
                std::ceil(static_cast<float>(band.lowerHz) / binWidth));
            if (i == 0) {
                lower = std::max<uint16_t>(1, lower);
            } else {
                lower = static_cast<uint16_t>(std::min<uint16_t>(
                    maxBin, std::max<uint16_t>(lower, previousUpper + 1U)));
            }

            auto upper = static_cast<uint16_t>(
                std::ceil(static_cast<float>(band.upperHz) / binWidth));
            upper = upper == 0 ? 0 : static_cast<uint16_t>(upper - 1U);
            upper = std::min<uint16_t>(upper, maxBin);
            if (upper < lower) {
                upper = lower;
            }

            const auto centerHz = (static_cast<float>(band.lowerHz) +
                                   static_cast<float>(band.upperHz)) *
                                  0.5F;
            _bandPlan[i] = BandPlan{
                .band = static_cast<FftBand>(i),
                .lowerHz = band.lowerHz,
                .upperHz = band.upperHz,
                .lowerBin = lower,
                .upperBin = upper,
                .preparationGain = _preparationGain(i, centerHz),
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

    [[nodiscard]] float _preparationGain(size_t bandIndex,
                                         float centerHz) const {
        float gain = 1.0F;
        const auto &pipeline = config().signalPipeline;
        if (pipeline.calibration.has_value()) {
            gain *= pipeline.calibration->bandGains[bandIndex];
        }
        if (pipeline.perceptualWeighting.has_value()) {
            gain *= _weighting(centerHz, *pipeline.perceptualWeighting);
        }
        return gain;
    }

    [[nodiscard]] float
    _weighting(float frequencyHz,
               const FftPerceptualWeightingConfig &weighting) const {
        if (weighting.weighting != FftWeighting::AWeighting) {
            return 1.0F;
        }
        if (frequencyHz <= 0.0F) {
            return 0.0F;
        }

        const auto freq2 =
            static_cast<double>(frequencyHz) * static_cast<double>(frequencyHz);
        const auto numerator = (12200.0 * 12200.0) * freq2 * freq2;
        const auto denominator =
            (freq2 + (20.6 * 20.6)) *
            std::sqrt((freq2 + (107.7 * 107.7)) * (freq2 + (737.9 * 737.9))) *
            (freq2 + (12200.0 * 12200.0));
        const auto ratio = numerator / denominator;
        const auto decibels = 2.0 + (20.0 * std::log10(ratio));
        const auto linear = static_cast<float>(std::pow(10.0, decibels / 20.0));
        return 1.0F + ((linear - 1.0F) * weighting.amount);
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
            if (_metrics != nullptr) {
                _metrics->addDroppedFrame();
            }
            return;
        }

        auto frame = FftResult{
            .audio = _source.audioInfo(),
            .sequence = _nextFrameSequence++,
            .timestampUs = static_cast<uint64_t>(startedUs),
            .length = config().length,
            .stride = config().stride,
        };

        const auto bandStartedUs = _profilingTimestampUs();
        for (size_t i = 0; i < fftBandCount; ++i) {
            frame.bands[i] = _computeBand(fft, _bandPlan[i]);
        }
        const auto bandElapsedUs = _profilingElapsedUs(bandStartedUs);
        _recordMax(_maxBandComputeUs, bandElapsedUs);
        if (_metrics != nullptr) {
            _metrics->addBandCompute(bandElapsedUs);
        }

        const auto cacheStartedUs = _profilingTimestampUs();
        _magnitudeCache.update(frame);
        const auto cacheElapsedUs = _profilingElapsedUs(cacheStartedUs);
        _recordMax(_maxMagnitudeCacheUs, cacheElapsedUs);
        if (_metrics != nullptr) {
            _metrics->addMagnitudeCache(cacheElapsedUs);
        }

        const auto beatUpdateStartedUs = _profilingTimestampUs();
        auto beatEvents = _beatTracker.update(frame);
        const auto beatUpdateElapsedUs =
            static_cast<uint32_t>(_profilingElapsedUs(beatUpdateStartedUs));
        _recordMax(_maxBeatUpdateUs, beatUpdateElapsedUs);
        if (_metrics != nullptr) {
            _metrics->addBeatUpdate(beatUpdateElapsedUs);
        }

        for (const auto &beat : beatEvents) {
            if (!beat.has_value()) {
                continue;
            }
            _beats.fetch_add(1, std::memory_order_acq_rel);
            _recordBeatStatus(*beat);
            if (_metrics != nullptr) {
                _metrics->addBeat(*beat, beat->group ==
                                             config().beatTracker.primaryGroup);
            }
            const auto beatDispatchStartedUs = _profilingTimestampUs();
            _dispatchBeat(*beat);
            const auto beatDispatchElapsedUs =
                _profilingElapsedUs(beatDispatchStartedUs);
            _recordMax(_maxBeatDispatchUs, beatDispatchElapsedUs);
            if (_metrics != nullptr) {
                _metrics->addBeatDispatch(beatDispatchElapsedUs);
            }
        }

        const auto dispatchStartedUs = _profilingTimestampUs();
        _dispatchFrame(frame);
        const auto dispatchElapsedUs = _profilingElapsedUs(dispatchStartedUs);
        _recordMax(_maxFrameDispatchUs, dispatchElapsedUs);
        if (_metrics != nullptr) {
            _metrics->addFrameDispatch(dispatchElapsedUs);
        }

        _frames.fetch_add(1, std::memory_order_acq_rel);
        const auto elapsedUs = _profilingElapsedUs(startedUs);
        _recordMax(_maxFrameUs, elapsedUs);
        if (_metrics != nullptr) {
            _metrics->addFrame(elapsedUs);
        }
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
            .weightedMagnitude =
                _compressMagnitude(magnitude * plan.preparationGain),
        };
    }

    [[nodiscard]] float _compressMagnitude(float magnitude) const {
        if (magnitude <= 0.0F || !std::isfinite(magnitude)) {
            return 0.0F;
        }

        const auto &compression = config().signalPipeline.compression;
        if (!compression.has_value()) {
            return magnitude;
        }

        switch (compression->mode) {
        case FftMagnitudeCompression::Sqrt:
            return std::sqrt(magnitude);
        case FftMagnitudeCompression::Log1p:
            return std::log1p(magnitude * compression->logScale);
        case FftMagnitudeCompression::Linear:
        default:
            return magnitude;
        }
    }

    void _dispatchFrame(const FftResult &frame) {
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

    void _dispatchBeat(const BeatResult &event) {
        if (event.group == config().beatTracker.primaryGroup) {
            _dispatchBeatHandler(config().beatIndicator, event,
                                 "Configured beat indicator");
        }
        for (const auto &handler : _beatHandlers) {
            _dispatchBeatHandler(handler, event, "Beat handler");
        }
    }

    void _dispatchBeatHandler(const BeatResultHandler &handler,
                              const BeatResult &event,
                              const char *label) const {
        if (!handler.valid()) {
            return;
        }
        auto ret = handler.callback(handler.owner, event);
        if (!ret.ok()) {
            _log_w("%s failed: " ERR_FMT, label, ERR_ARG(ret));
        }
    }

    [[nodiscard]] BeatGroupStatus _groupBeatStatus(size_t index,
                                                   uint32_t nowMs) const {
        const auto lastBeatMs =
            _groupLastBeatMs[index].load(std::memory_order_acquire);
        return BeatGroupStatus{
            .hasBeat = lastBeatMs != 0,
            .beats = _groupBeats[index].load(std::memory_order_acquire),
            .bpmHundredths =
                _groupBpmHundredths[index].load(std::memory_order_acquire),
            .energy = static_cast<uint8_t>(
                _groupLastBeatEnergy[index].load(std::memory_order_acquire)),
            .lastBeatAgeMs = lastBeatMs == 0 ? 0U : nowMs - lastBeatMs,
        };
    }

    void _recordBeatStatus(const BeatResult &event) {
        const auto index = beatGroupIndex(event.group);
        if (index >= beatGroupCount) {
            return;
        }
        _groupBeats[index].fetch_add(1, std::memory_order_acq_rel);
        _groupLastBeatEnergy[index].store(event.energy,
                                          std::memory_order_release);
        _groupLastBeatMs[index].store(
            static_cast<uint32_t>(event.timestampUs / 1000ULL),
            std::memory_order_release);
        _groupBpmHundredths[index].store(
            static_cast<uint32_t>(std::lround(event.bpm * 100.0F)),
            std::memory_order_release);
    }

    static int64_t _profilingTimestampUs() {
        if constexpr (Metrics::profilingEnabled) {
            return ::platform::get_time_us();
        }
        return 0;
    }

    static uint32_t _profilingElapsedUs(int64_t startedUs) {
        if constexpr (Metrics::profilingEnabled) {
            const auto nowUs = ::platform::get_time_us();
            if (nowUs >= startedUs) {
                return static_cast<uint32_t>(nowUs - startedUs);
            }
        }
        return 0;
    }

    static void _recordMax(std::atomic<uint32_t> &target, uint32_t value) {
        if constexpr (!Metrics::profilingEnabled) {
            return;
        }
        auto current = target.load(std::memory_order_acquire);
        while (value > current && !target.compare_exchange_weak(
                                      current, value, std::memory_order_acq_rel,
                                      std::memory_order_acquire)) {
        }
    }

    void _resetStats() {
        _copyCalls.store(0, std::memory_order_release);
        _copiedBytes.store(0, std::memory_order_release);
        _emptyCopies.store(0, std::memory_order_release);
        _readinessProbes.store(0, std::memory_order_release);
        _sourceUnavailableSkips.store(0, std::memory_order_release);
        _frames.store(0, std::memory_order_release);
        _droppedFrames.store(0, std::memory_order_release);
        _beats.store(0, std::memory_order_release);
        for (size_t i = 0; i < beatGroupCount; ++i) {
            _groupBeats[i].store(0, std::memory_order_release);
            _groupLastBeatEnergy[i].store(0, std::memory_order_release);
            _groupLastBeatMs[i].store(0, std::memory_order_release);
            _groupBpmHundredths[i].store(0, std::memory_order_release);
        }
        _maxCopyUs.store(0, std::memory_order_release);
        _maxReadinessProbeUs.store(0, std::memory_order_release);
        _maxFrameUs.store(0, std::memory_order_release);
        _maxBandComputeUs.store(0, std::memory_order_release);
        _maxMagnitudeCacheUs.store(0, std::memory_order_release);
        _maxFrameDispatchUs.store(0, std::memory_order_release);
        _maxBeatUpdateUs.store(0, std::memory_order_release);
        _maxBeatDispatchUs.store(0, std::memory_order_release);
        _nextFrameSequence = 1;
        _handlingFrame.store(false, std::memory_order_release);
    }

    IAudioSource &_source;
    FftBackend _fftBackend;
    Platform::StreamCopier _copier{0};
    Platform::HammingWindow _hamming;
    Platform::HannWindow _hann;
    std::array<BandPlan, fftBandCount> _bandPlan{};
    MagnitudeCache _magnitudeCache{};
    BeatTracker _beatTracker{};
    std::array<FftResultHandler, fftMaxFrameHandlers> _frameHandlers{};
    std::array<BeatResultHandler, fftMaxBeatHandlers> _beatHandlers{};
    Metrics *_metrics = nullptr;
    Totem::TaskController::RunnerKey _task = 0;
    uint32_t _nextFrameSequence = 1;
    std::atomic<bool> _handlingFrame{false};
    std::atomic<uint32_t> _copyCalls{0};
    std::atomic<uint32_t> _copiedBytes{0};
    std::atomic<uint32_t> _emptyCopies{0};
    std::atomic<uint32_t> _readinessProbes{0};
    std::atomic<uint32_t> _sourceUnavailableSkips{0};
    std::atomic<uint32_t> _frames{0};
    std::atomic<uint32_t> _droppedFrames{0};
    std::atomic<uint32_t> _beats{0};
    std::array<std::atomic<uint32_t>, beatGroupCount> _groupBeats{};
    std::array<std::atomic<uint32_t>, beatGroupCount> _groupLastBeatEnergy{};
    std::array<std::atomic<uint32_t>, beatGroupCount> _groupLastBeatMs{};
    std::array<std::atomic<uint32_t>, beatGroupCount> _groupBpmHundredths{};
    std::atomic<uint32_t> _maxCopyUs{0};
    std::atomic<uint32_t> _maxReadinessProbeUs{0};
    std::atomic<uint32_t> _maxFrameUs{0};
    std::atomic<uint32_t> _maxBandComputeUs{0};
    std::atomic<uint32_t> _maxMagnitudeCacheUs{0};
    std::atomic<uint32_t> _maxFrameDispatchUs{0};
    std::atomic<uint32_t> _maxBeatUpdateUs{0};
    std::atomic<uint32_t> _maxBeatDispatchUs{0};
};

inline constexpr CommandsContract<FftAnalyzer, Commands<FftAnalyzer>>
    _fft_analyzer_commands_contract;

} // namespace Totem::Audio::detail
