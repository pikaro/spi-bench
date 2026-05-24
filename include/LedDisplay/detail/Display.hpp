#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include "LedDisplay/Interfaces/Color.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Outputs/FastLedOutput.hpp"
#include "LedDisplay/detail/AnimationEngine.hpp"
#include "Macros/Facade.hpp"
#include "Platform/Gpio.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "Services/PubSub.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "Types/Error.hpp"
#include "Types/Gpio.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace Totem::LedDisplay::detail {

class Display : public HasLifecycle<Display, Config>,
                public HasTaskController<Display, Config> {
    friend class HasLifecycle<Display, Config>;
    friend struct LifecycleContract<Display, Config>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Display, Config>;
    friend struct TaskController::TaskHooks::Contract<Display>;
    friend struct TaskControllerContract<Display>;

  public:
    explicit Display(TaskController::IRegistry &registry)
        : HasTaskController<Display, Config>(registry) {}

    DELETE_COPY(Display)
    DELETE_MOVE(Display)

    static constexpr const char *name = "LedDisplay";
    static constexpr LogComponent logComponent = LogComponent::Output;
    static constexpr uint8_t maxFastLedDataLines = 2;
    static constexpr uint32_t presentMissLogIntervalMs = 1000;
    static constexpr uint32_t ditherErrorLogIntervalMs = 1000;

    ReturnCode submitAnimationCommand(const AnimationCommand &cmd) {
        FAIL_IF_INACTIVE_ERR("Cannot submit LED animation command before %s "
                             "begins",
                             name);
        return _engine.submit(cmd);
    }

    ReturnCode subscribePubSub() {
        if (_pubSubSubscribed) {
            return OK();
        }
        FAIL_IF_NOT(PubSubService::configured(), ERR(CoreError, InvalidState),
                    "PubSub backend is not configured");

        auto &pubSub = PubSubService::get();
        auto animationSub = pubSub.subscribe(
            "led-anim", {.subscriber = this, .callback = _onAnimationEnvelope},
            PubSubService::Topic::Animation);
        if (!animationSub) {
            return animationSub.error();
        }
        _animationSub = *animationSub;

        auto fftSub = pubSub.subscribe(
            "led-fft",
            {.subscriber = &_engine,
             .callback = AnimationEngine::onFftEnvelope},
            PubSubService::Topic::FftFrame);
        if (!fftSub) {
            (void)pubSub.unsubscribe(_animationSub);
            _animationSub = 0;
            return fftSub.error();
        }
        _fftSub = *fftSub;
        _pubSubSubscribed = true;
        _log_i("LedDisplay subscribed to animation and FFT PubSub topics");
        return OK();
    }

    ReturnCode beginPresentStrobe(Pin pin,
                                  GpioPull pull = GpioPull::Down) {
        FAIL_IF_INACTIVE_ERR("Cannot initialize LED present strobe before %s "
                             "begins",
                             name);
        if (_presentStrobeEnabled) {
            return OK();
        }
        FAIL_IF_ERR_FWD(
            _presentStrobeGpio.initInput(pin, pull, GpioInterrupt::Rising),
            "Failed to initialize LED present strobe input");
        FAIL_IF_ERR_FWD(
            _presentStrobeGpio.registerIsr(this, _onPresentStrobeIsr),
            "Failed to register LED present strobe ISR");
        _presentStrobeEnabled = true;
        _log_i("LedDisplay present strobe input ready on pin " SV_FMT,
               MAGIC_SV_ARG(pin));
        return OK();
    }

  private:
    ReturnCode _onBegin() {
        static_assert(Config::dataLineCount <= maxFastLedDataLines,
                      "FastLED output currently supports two configured lines");
        DEFAULT_TASK();
        _renderTask = task;
        FAIL_IF_ERR_FWD(_engine.begin(),
                        "Failed to initialize LED animation engine");
        FAIL_IF_ERR_FWD(_output.begin(config()),
                        "Failed to initialize LED output backend");
        START_TASK();
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        ret.combine(_presentStrobeGpio.deinit());
        _presentStrobeEnabled = false;
        ret.combine(_unsubscribePubSub());
        ret.combine(this->_endTaskController());
        ret.combine(_engine.end());
        ret.combine(_output.deinit());
        return ret;
    }

    ReturnCode _unsubscribePubSub() {
        if (!_pubSubSubscribed || !PubSubService::configured()) {
            _pubSubSubscribed = false;
            _animationSub = 0;
            _fftSub = 0;
            return OK();
        }
        auto ret = OK();
        auto &pubSub = PubSubService::get();
        if (_animationSub != 0) {
            ret.combine(pubSub.unsubscribe(_animationSub));
        }
        if (_fftSub != 0) {
            ret.combine(pubSub.unsubscribe(_fftSub));
        }
        _animationSub = 0;
        _fftSub = 0;
        _pubSubSubscribed = false;
        return ret;
    }

    static ReturnCode
    _onAnimationEnvelope(void *owner,
                         const Totem::PubSubBackend::Envelope &envelope) {
        auto *self = static_cast<Display *>(owner);
        FAIL_IF_NULL(self, ERR(CoreError, InvalidArgument),
                     "LedDisplay animation subscriber owner is null");
        FAIL_IF_UNEXPECTED_FWD(cmd, envelope.getPayloadAs<AnimationCommand>(),
                               "Failed to decode animation command");
        return self->_engine.submit(cmd);
    }

    static void _onPresentStrobeIsr(void *owner, GpioEvent event) {
        auto *self = static_cast<Display *>(owner);
        if (self == nullptr || !event.level) {
            return;
        }
        self->_pendingPresentStrobes.fetch_add(1, std::memory_order_relaxed);
        Totem::TaskController::Controller::signalTaskFromIsr(
            self->_renderTask, Signal::Ready);
    }

    ReturnCode _onTaskStep() {
        const auto nowMs = ::platform::get_time();
        const auto nowUs = ::platform::get_time_us();
        if (_presentStrobeEnabled && !_consumePresentStrobe(nowMs)) {
            return OK();
        }
        _checkDitherCadence(nowMs, nowUs);

        FAIL_IF_ERR_FWD(_engine.render(nowMs, _frame),
                        "Failed to render LED animation frame");

        const auto showStartUs = ::platform::get_time_us();
        FAIL_IF_ERR_FWD(_output.show(_frame),
                        "Failed to show LED output frame");
        const auto showUs = ::platform::get_time_us() - showStartUs;
        _maxShowUs = std::max(_maxShowUs, static_cast<uint32_t>(showUs));
        ++_frames;
        return OK();
    }

    [[nodiscard]] bool _consumePresentStrobe(uint32_t nowMs) {
        const auto pending =
            _pendingPresentStrobes.exchange(0, std::memory_order_acq_rel);
        if (pending == 0) {
            return false;
        }
        ++_presentStrobeFrames;
        if (pending <= 1) {
            return true;
        }

        _missedPresentStrobes += pending - 1U;
        if (nowMs - _lastPresentMissLogMs >= presentMissLogIntervalMs) {
            _lastPresentMissLogMs = nowMs;
            _log_e("LED render missed %lu present strobes; pending=%lu "
                   "totalMissed=%lu",
                   static_cast<unsigned long>(pending - 1U),
                   static_cast<unsigned long>(pending),
                   static_cast<unsigned long>(_missedPresentStrobes));
        }
        return true;
    }

    void _checkDitherCadence(uint32_t nowMs, uint64_t nowUs) {
        if constexpr (!Config::temporalDithering) {
            return;
        }
        if (_lastFrameUs == 0) {
            _lastFrameUs = nowUs;
            return;
        }

        const auto elapsedUs = nowUs - _lastFrameUs;
        _lastFrameUs = nowUs;
        if (elapsedUs <= Config::frameBudgetUs) {
            return;
        }
        if (nowMs - _lastDitherErrorMs < ditherErrorLogIntervalMs) {
            return;
        }
        _lastDitherErrorMs = nowMs;
        _log_e("LED frame cadence below dither threshold: %llu us > %lu us",
               static_cast<unsigned long long>(elapsedUs),
               static_cast<unsigned long>(Config::frameBudgetUs));
    }

    Outputs::FastLedOutput _output;
    AnimationEngine _engine{};
    std::array<HsvColor, Config::ownedPixelCount> _frame{};
    bool _pubSubSubscribed = false;
    bool _presentStrobeEnabled = false;
    Totem::PubSubBackend::SubscriberKey _animationSub = 0;
    Totem::PubSubBackend::SubscriberKey _fftSub = 0;
    ::platform::Gpio _presentStrobeGpio;
    Totem::TaskController::RunnerKey _renderTask = 0;
    std::atomic<uint32_t> _pendingPresentStrobes{0};
    uint64_t _lastFrameUs = 0;
    uint32_t _lastDitherErrorMs = 0;
    uint32_t _lastPresentMissLogMs = 0;
    uint32_t _presentStrobeFrames = 0;
    uint32_t _missedPresentStrobes = 0;
    uint32_t _maxShowUs = 0;
    uint32_t _frames = 0;
};

inline constexpr LifecycleContract<Display, Config>
    _led_display_lifecycle_contract;
inline constexpr TaskControllerContract<Display>
    _led_display_task_controller_contract;
inline constexpr TaskController::TaskHooks::Contract<Display>
    _led_display_task_hooks_contract;

} // namespace Totem::LedDisplay::detail
