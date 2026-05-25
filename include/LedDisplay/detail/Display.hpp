#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include "LedDisplay/Interfaces/Color.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Outputs/FastLedOutput.hpp"
#include "LedDisplay/detail/AnimationEngine.hpp"
#include "LedDisplay/detail/Metrics.hpp"
#include "LedDisplay/detail/PresentBuffers.hpp"
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
    static constexpr uint32_t frameBudgetLogIntervalMs = 1000;

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

        auto inputSub = _engine.subscribePubSubInputs();
        if (!inputSub.ok()) {
            (void)pubSub.unsubscribe(_animationSub);
            _animationSub = 0;
            return inputSub;
        }
        _pubSubSubscribed = true;
        _log_i("LedDisplay subscribed to animation command topic");
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
        prewarmMetrics();
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
        auto ret = OK();
        if (!_pubSubSubscribed || !PubSubService::configured()) {
            _pubSubSubscribed = false;
            _animationSub = 0;
            ret.combine(_engine.unsubscribePubSubInputs());
            return ret;
        }
        auto &pubSub = PubSubService::get();
        ret.combine(_engine.unsubscribePubSubInputs());
        if (_animationSub != 0) {
            ret.combine(pubSub.unsubscribe(_animationSub));
        }
        _animationSub = 0;
        _pubSubSubscribed = false;
        return ret;
    }

    static ReturnCode
    _onAnimationEnvelope(void *owner,
                         const Totem::PubSubBackend::Envelope &envelope) {
        auto *self = static_cast<Display *>(owner);
        FAIL_IF_NULL(self, ERR(CoreError, InvalidArgument),
                     "LedDisplay animation subscriber owner is null");
        auto cmd = envelope.getPayloadAs<AnimationCommand>();
        if (!cmd) {
            metrics().addBadCommand();
            FAIL_ERR_FWD(cmd.error(), "Failed to decode animation command");
        }
        return self->_engine.submit(*cmd);
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
        if (_presentStrobeEnabled && !_consumePresentStrobe(nowMs)) {
            return OK();
        }

        const auto frameStartUs = ::platform::get_time_us();
        auto ret = _runFrameStep(nowMs);
        const auto frameUs =
            static_cast<uint32_t>(::platform::get_time_us() - frameStartUs);
        metrics().recordFrameDuration(frameUs);
        if (frameUs > this->config().frameBudgetUs) {
            metrics().addOverBudgetFrame();
            _logOverBudgetFrame(nowMs, frameUs);
        }
        return ret;
    }

    ReturnCode _runFrameStep(uint32_t nowMs) {
        if constexpr (Config::presentBufferMode == PresentBufferMode::None) {
            FAIL_IF_ERR_FWD(_renderFrame(nowMs),
                            "Failed to render LED animation frame");
            return _presentFrame();
        }

        if (!_presentBuffers.hasPresentedFrame()) {
            FAIL_IF_ERR_FWD(_renderFrame(nowMs),
                            "Failed to render initial LED animation frame");
        }

        FAIL_IF_ERR_FWD(_presentFrame(),
                        "Failed to present LED animation frame");
        FAIL_IF_ERR_FWD(_renderFrame(::platform::get_time()),
                        "Failed to render next LED animation frame");
        return OK();
    }

    ReturnCode _renderFrame(uint32_t nowMs) {
        const auto renderStartUs = ::platform::get_time_us();
        auto ret = _engine.render(nowMs, _presentBuffers.renderTarget());
        const auto renderUs = static_cast<uint32_t>(
            ::platform::get_time_us() - renderStartUs);
        metrics().recordRenderDuration(renderUs);
        if (!ret.ok()) {
            metrics().addRenderFailure();
            FAIL_ERR_FWD(ret, "Failed to render LED animation frame");
        }
        _presentBuffers.publishRenderedFrame();
        return OK();
    }

    ReturnCode _presentFrame() {
        const auto frame = _presentBuffers.selectForPresent();
        FAIL_IF(frame.frame.empty(), ERR(CoreError, InvalidState),
                "No LED frame is ready for presentation");
        if (frame.repeated) {
            ++_repeatedPresentFrames;
            metrics().addRepeatedPresent();
        }

        const auto showStartUs = ::platform::get_time_us();
        auto ret = _output.show(frame.frame);
        const auto showUs = ::platform::get_time_us() - showStartUs;
        _maxShowUs = std::max(_maxShowUs, static_cast<uint32_t>(showUs));
        metrics().recordShowDuration(static_cast<uint32_t>(showUs));
        if (!ret.ok()) {
            metrics().addShowFailure();
            FAIL_ERR_FWD(ret, "Failed to show LED output frame");
        }
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
        metrics().addMissedStrobes(pending - 1U);
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

    void _logOverBudgetFrame(uint32_t nowMs, uint32_t frameUs) {
        if (nowMs - _lastFrameBudgetLogMs < frameBudgetLogIntervalMs) {
            return;
        }
        _lastFrameBudgetLogMs = nowMs;
        _log_e("LED frame work exceeded frame budget: %lu us > %lu us",
               static_cast<unsigned long>(frameUs),
               static_cast<unsigned long>(this->config().frameBudgetUs));
    }

    Outputs::FastLedOutput _output;
    AnimationEngine _engine{};
    PresentBuffers _presentBuffers{};
    bool _pubSubSubscribed = false;
    bool _presentStrobeEnabled = false;
    Totem::PubSubBackend::SubscriberKey _animationSub = 0;
    ::platform::Gpio _presentStrobeGpio;
    Totem::TaskController::RunnerKey _renderTask = 0;
    std::atomic<uint32_t> _pendingPresentStrobes{0};
    uint32_t _lastFrameBudgetLogMs = 0;
    uint32_t _lastPresentMissLogMs = 0;
    uint32_t _presentStrobeFrames = 0;
    uint32_t _missedPresentStrobes = 0;
    uint32_t _repeatedPresentFrames = 0;
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
