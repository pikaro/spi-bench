#pragma once

#include "Audio/Interfaces/DisplayConfig.hpp"
#include "Audio/Interfaces/Types.hpp"
#include "Audio/detail/DisplayMetrics.hpp"
#include "Audio/detail/FftAnalyzer.hpp"
#include "Audio/detail/Types.hpp"
#include "Base/HasLifecycle.hpp"
#include "Base/HasMutex.hpp"
#include "Base/HasTaskController.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "TaskController/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include "Wire/I2C/Facade.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace Totem::Audio::detail {

class FftDisplay : public HasLifecycle<FftDisplay, FftDisplayConfig>,
                   public HasTaskController<FftDisplay, FftDisplayConfig>,
                   public HasMutex<FftDisplay> {
    friend class HasLifecycle<FftDisplay, FftDisplayConfig>;
    friend struct LifecycleContract<FftDisplay, FftDisplayConfig>;
    friend struct MutexContract<FftDisplay>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<FftDisplay, FftDisplayConfig>;
    friend struct TaskControllerContract<FftDisplay>;
    friend struct TaskController::TaskHooks::Contract<FftDisplay>;

  public:
    DELETE_COPY(FftDisplay)
    DELETE_MOVE(FftDisplay)

    static constexpr const char *name = "Audio::FftDisplay";
    static constexpr LogComponent logComponent =
        Totem::Audio::detail::logComponent;

    FftDisplay(TaskController::IRegistry &registry, FftAnalyzer &analyzer,
               Totem::Wire::I2C::Ssd1306Display &display)
        : HasTaskController<FftDisplay, FftDisplayConfig>(registry),
          _analyzer(analyzer), _display(display) {}

  private:
    ReturnCode _onBegin() {
        (void)displayMetrics();
        FAIL_IF(!_display.active(), ERR(CoreError, InvalidState),
                "Cannot begin FFT display before SSD1306 display is active");
        _defaultMutexTimeoutMs = 2;

        if (!_registeredFrameHandler) {
            FAIL_IF_ERR_FWD(_analyzer.addFrameHandler(FftResultHandler{
                                .owner = this,
                                .callback = _onFrame,
                            }),
                            "Failed to register FFT display frame handler");
            _registeredFrameHandler = true;
        }

        if (!_registeredBeatHandler) {
            FAIL_IF_ERR_FWD(_analyzer.addBeatHandler(BeatResultHandler{
                                .owner = this,
                                .callback = _onBeat,
                            }),
                            "Failed to register FFT display beat handler");
            _registeredBeatHandler = true;
        }

        _latestFrame = {};
        _hasFrame.store(false, std::memory_order_release);
        _capturedFrames.store(0, std::memory_order_release);
        _droppedFrames.store(0, std::memory_order_release);
        for (size_t i = 0; i < beatGroupCount; ++i) {
            _beatUntilMs[i].store(0, std::memory_order_release);
            _beatRanges[i].store(0, std::memory_order_release);
        }
        _acceptingFrames.store(true, std::memory_order_release);

        DEFAULT_TASK();
        _task = task;
        START_TASK();

        _log_i("FFT display started at %u ms refresh interval",
               config().task.intervalMs);
        return OK();
    }

    ReturnCode _onEnd() {
        _acceptingFrames.store(false, std::memory_order_release);
        _task = 0;
        auto ret = this->_endTaskController();
        _display.clear();
        ret.combine(_display.flush());
        return ret;
    }

    ReturnCode _onTaskStep() {
        FftResult frame{};
        {
            auto guard = _mutexGuard(2);
            if (!guard.acquired()) {
                return OK();
            }
            if (!_hasFrame.load(std::memory_order_acquire)) {
                return OK();
            }
            frame = _latestFrame;
        }

        _drawFrame(frame, ::platform::get_time());
        const auto startedUs = _profilingTimestampUs();
        auto ret = _display.flush();
        displayMetrics().addFlush(ret, _profilingElapsedUs(startedUs));
        return ret;
    }

    static ReturnCode _onFrame(void *owner, const FftResult &frame) {
        auto *self = static_cast<FftDisplay *>(owner);
        if (self == nullptr) {
            return ERR(CoreError, InvalidArgument);
        }
        return self->_captureFrame(frame);
    }

    static ReturnCode _onBeat(void *owner, const BeatResult &event) {
        auto *self = static_cast<FftDisplay *>(owner);
        if (self == nullptr) {
            return ERR(CoreError, InvalidArgument);
        }
        return self->_captureBeat(event);
    }

    ReturnCode _captureFrame(const FftResult &frame) {
        if (!_acceptingFrames.load(std::memory_order_acquire)) {
            return OK();
        }

        auto guard = _mutexGuard(0);
        if (!guard.acquired()) {
            _droppedFrames.fetch_add(1, std::memory_order_acq_rel);
            displayMetrics().addDroppedFrame();
            return OK();
        }
        _latestFrame = frame;
        _hasFrame.store(true, std::memory_order_release);
        _capturedFrames.fetch_add(1, std::memory_order_acq_rel);
        displayMetrics().addCapturedFrame();
        return OK();
    }

    ReturnCode _captureBeat(const BeatResult &event) {
        if (!_acceptingFrames.load(std::memory_order_acquire)) {
            return OK();
        }
        const auto index = beatGroupIndex(event.group);
        if (index >= beatGroupCount) {
            return OK();
        }
        _beatRanges[index].store(_packRange(event.bands),
                                 std::memory_order_release);
        _beatUntilMs[index].store(::platform::get_time() +
                                      config().beatBarHoldMs,
                                  std::memory_order_release);
        displayMetrics().addBeat();
        return OK();
    }

    [[nodiscard]] bool _beatActive(size_t groupIndex, uint32_t nowMs) const {
        return static_cast<int32_t>(
                   _beatUntilMs[groupIndex].load(std::memory_order_acquire) -
                   nowMs) > 0;
    }

    void _drawFrame(const FftResult &frame, uint32_t nowMs) {
        const auto width = _display.width();
        const auto height = _display.height();

        _display.clear();
        if (config().showRawBands) {
            _drawRawAndEffectiveBands(frame, width, height);
        } else {
            _drawEffectiveBands(frame, width, height);
        }

        _drawBeatBars(nowMs, width);
    }

    void _drawRawAndEffectiveBands(const FftResult &frame, uint8_t width,
                                   uint8_t height) {
        const auto gap = config().barGapPx;
        const auto intraBandGap = static_cast<uint8_t>(gap / 2);
        const auto totalGap = static_cast<uint16_t>(gap) *
                              static_cast<uint16_t>(fftBandCount - 1);
        const auto availableWidth =
            width > totalGap ? static_cast<uint16_t>(width - totalGap) : width;
        const auto barWidth = static_cast<uint8_t>(
            std::max<uint16_t>(1, availableWidth / (fftBandCount * 2U)));
        const auto maxMagnitude = _maxRawMagnitude(frame);

        for (uint8_t index = 0; index < fftBandCount; ++index) {
            const auto x = static_cast<uint8_t>(
                index * ((static_cast<uint16_t>(barWidth) * 2U) + gap));
            const auto rawRatio =
                maxMagnitude > 0.0F
                    ? frame.bands[index].magnitude / maxMagnitude
                    : 0.0F;
            const auto rawWidth = static_cast<uint8_t>(std::max<uint16_t>(
                1, barWidth > intraBandGap ? barWidth - intraBandGap : 1));
            _drawVerticalBar(x, rawWidth, height, rawRatio);

            const auto scaledRatio =
                static_cast<float>(frame.bands[index].scaled) / 255.0F;
            _drawVerticalBar(static_cast<uint8_t>(x + barWidth), barWidth,
                             height, scaledRatio);
        }
    }

    void _drawEffectiveBands(const FftResult &frame, uint8_t width,
                             uint8_t height) {
        const auto gap = config().barGapPx;
        const auto totalGap = static_cast<uint16_t>(gap) *
                              static_cast<uint16_t>(fftBandCount - 1);
        const auto availableWidth =
            width > totalGap ? static_cast<uint16_t>(width - totalGap) : width;
        const auto barWidth = static_cast<uint8_t>(
            std::max<uint16_t>(1, availableWidth / fftBandCount));

        for (uint8_t index = 0; index < fftBandCount; ++index) {
            const auto ratio =
                static_cast<float>(frame.bands[index].scaled) / 255.0F;
            const auto x = static_cast<uint8_t>(index * (barWidth + gap));
            _drawVerticalBar(x, barWidth, height, ratio);
        }
    }

    void _drawVerticalBar(uint8_t x, uint8_t width, uint8_t height,
                          float ratio) {
        const auto barHeight = _scaledBarHeight(ratio, height);
        const auto y = static_cast<uint8_t>(height - barHeight);
        _display.fillRect(x, y, width, barHeight);
    }

    void _drawBeatBars(uint32_t nowMs, uint8_t displayWidth) {
        for (size_t i = 0; i < beatGroupCount; ++i) {
            if (!_beatActive(i, nowMs)) {
                continue;
            }
            const auto range =
                _unpackRange(_beatRanges[i].load(std::memory_order_acquire));
            const auto x = _bandRangeX(range, displayWidth);
            const auto width = _bandRangeWidth(range, displayWidth, x);
            _display.fillRect(x, 0, width, 2);
        }
    }

    [[nodiscard]] uint8_t _bandRangeX(FftBandIndexRange range,
                                      uint8_t displayWidth) const {
        const auto gap = config().barGapPx;
        const auto totalGap = static_cast<uint16_t>(gap) *
                              static_cast<uint16_t>(fftBandCount - 1);
        const auto availableWidth =
            displayWidth > totalGap
                ? static_cast<uint16_t>(displayWidth - totalGap)
                : displayWidth;
        const auto divisor =
            config().showRawBands ? fftBandCount * 2U : fftBandCount;
        const auto barWidth = std::max<uint16_t>(1, availableWidth / divisor);
        const auto slotWidth =
            config().showRawBands ? (barWidth * 2U) + gap : barWidth + gap;
        return static_cast<uint8_t>(
            std::min<uint16_t>(displayWidth, range.lower * slotWidth));
    }

    [[nodiscard]] uint8_t _bandRangeWidth(FftBandIndexRange range,
                                          uint8_t displayWidth,
                                          uint8_t x) const {
        const auto gap = config().barGapPx;
        const auto totalGap = static_cast<uint16_t>(gap) *
                              static_cast<uint16_t>(fftBandCount - 1);
        const auto availableWidth =
            displayWidth > totalGap
                ? static_cast<uint16_t>(displayWidth - totalGap)
                : displayWidth;
        const auto divisor =
            config().showRawBands ? fftBandCount * 2U : fftBandCount;
        const auto barWidth = std::max<uint16_t>(1, availableWidth / divisor);
        const auto slotWidth =
            config().showRawBands ? (barWidth * 2U) + gap : barWidth + gap;
        const auto end = (static_cast<uint16_t>(range.upper) * slotWidth) +
                         (config().showRawBands ? barWidth * 2U : barWidth);
        if (end <= x) {
            return 1;
        }
        return static_cast<uint8_t>(
            std::max<uint16_t>(1, std::min<uint16_t>(displayWidth, end) - x));
    }

    [[nodiscard]] static uint32_t _packRange(FftBandIndexRange range) {
        return static_cast<uint32_t>(range.lower) |
               (static_cast<uint32_t>(range.upper) << 8U);
    }

    [[nodiscard]] static FftBandIndexRange _unpackRange(uint32_t packed) {
        return FftBandIndexRange{
            .lower = static_cast<uint8_t>(packed & 0xFFU),
            .upper = static_cast<uint8_t>((packed >> 8U) & 0xFFU),
        };
    }

    [[nodiscard]] static float _maxRawMagnitude(const FftResult &frame) {
        float maxMagnitude = 0.0F;
        for (const auto &band : frame.bands) {
            if (std::isfinite(band.magnitude)) {
                maxMagnitude = std::max(maxMagnitude, band.magnitude);
            }
        }
        return maxMagnitude;
    }

    [[nodiscard]] static uint8_t _scaledBarHeight(float ratio,
                                                  uint8_t panelHeight) {
        if (ratio <= 0.0F || !std::isfinite(ratio)) {
            return 0;
        }
        const auto clamped = std::clamp(ratio, 0.0F, 1.0F);
        return static_cast<uint8_t>(std::max<long>(
            1, std::lround(clamped * static_cast<float>(panelHeight))));
    }

    static int64_t _profilingTimestampUs() {
        if constexpr (DisplayMetrics::profilingEnabled) {
            return ::platform::get_time_us();
        }
        return 0;
    }

    static uint32_t _profilingElapsedUs(int64_t startedUs) {
        if constexpr (DisplayMetrics::profilingEnabled) {
            const auto nowUs = ::platform::get_time_us();
            if (nowUs >= startedUs) {
                return static_cast<uint32_t>(nowUs - startedUs);
            }
        }
        return 0;
    }

    FftAnalyzer &_analyzer;
    Totem::Wire::I2C::Ssd1306Display &_display;
    FftResult _latestFrame{};
    Totem::TaskController::RunnerKey _task = 0;
    bool _registeredFrameHandler = false;
    bool _registeredBeatHandler = false;
    std::atomic<bool> _acceptingFrames{false};
    std::atomic<bool> _hasFrame{false};
    std::atomic<uint32_t> _capturedFrames{0};
    std::atomic<uint32_t> _droppedFrames{0};
    std::array<std::atomic<uint32_t>, beatGroupCount> _beatUntilMs{};
    std::array<std::atomic<uint32_t>, beatGroupCount> _beatRanges{};
};

} // namespace Totem::Audio::detail
