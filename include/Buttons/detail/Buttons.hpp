#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "Buttons/Interfaces/Config.hpp"
#include "Buttons/Interfaces/Wire.hpp"
#include "Buttons/detail/Metrics.hpp"
#include "Buttons/detail/Types.hpp"
#include "Data.hpp"
#include "Macros/Facade.hpp"
#include "Platform/Gpio.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "Queue/Facade.hpp"
#include "Services/PubSub.hpp"
#include "StaticConfig/Button.hpp"
#include "TaskController/Facade.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "TaskController/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include "Types/Gpio.hpp"
#include "Types/Signal.hpp"
#include <array>
#include <cstddef>
#include <optional>

namespace Totem::Buttons::detail {

class Buttons : public HasLifecycle<Buttons, Config>,
                public HasTaskController<Buttons, Config> {
    friend class HasLifecycle<Buttons, Config>;
    friend struct LifecycleContract<Buttons, Config>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Buttons, Config>;
    friend struct TaskController::TaskHooks::Contract<Buttons>;
    friend struct TaskControllerContract<Buttons>;

    using MessagePool = Totem::PubSubBackend::Pool<ButtonEvent, 8>;

  public:
    explicit Buttons(TaskController::IRegistry &registry)
        : HasTaskController<Buttons, Config>(registry),
          _messagePool(PubSubService::nextMessageId) {}

    DELETE_COPY(Buttons)
    DELETE_MOVE(Buttons)

    static constexpr const char *name = "Buttons";

  private:
    ReturnCode _onBegin() {
        (void)metrics();
        DEFAULT_TASK();
        _task = task;
        INIT_QUEUE_OR_FAIL(_eventQueue);

        for (size_t i = 0; i < config().buttons.size(); ++i) {
            GpioInterrupt intr = GpioInterrupt::Disabled;

            const auto &btn = config().buttons[i];

            if (btn.notifyPressed && btn.notifyReleased) {
                intr = GpioInterrupt::AnyEdge;
            } else if (btn.notifyPressed || btn.notifyReleased) {
                const bool notifyOnRising = btn.notifyPressed ^ btn.activeLow;

                intr = notifyOnRising ? GpioInterrupt::Rising
                                      : GpioInterrupt::Falling;
            }

            FAIL_IF(intr == GpioInterrupt::Disabled, ERR(InvalidArgument),
                    "Button %u is configured to not notify on either press or "
                    "release",
                    btn.button);
            FAIL_IF_ERR_FWD(_gpios[i].initInput(btn.pin, btn.pull, intr),
                            "Failed to initialize GPIO for button %u",
                            btn.button);
            FAIL_IF_ERR_FWD(_gpios[i].registerIsr(this, _isrCallback),
                            "Failed to set ISR callback for button %u",
                            btn.button);
            FAIL_IF_UNEXPECTED_FWD(initialLevel, _gpios[i].level(),
                                   "Failed to read initial GPIO level for "
                                   "button %u",
                                   btn.button);
            _lastLevels[i] = initialLevel;
            _levelsKnown[i] = true;
            _log_i("Configured button " SV_FMT " on pin " SV_FMT
                   " (%u) with pull " SV_FMT " and interrupt " SV_FMT
                   "; initial level=%u",
                   MAGIC_SV_ARG(btn.button), MAGIC_SV_ARG(btn.pin), btn.pin,
                   MAGIC_SV_ARG(btn.pull), MAGIC_SV_ARG(intr),
                   initialLevel ? 1 : 0);
        }

        START_TASK();
        return OK();
    }

    static ReturnCode _onTaskNotify(Signal /*signal*/) { return OK(); }

    ReturnCode _onEnd() {
        auto ret = OK();
        ret.combine(this->_endTaskController());
        for (size_t i = 0; i < config().buttons.size(); ++i) {
            if (auto deinitRet = _gpios[i].deinit(); !deinitRet) {
                _log_e("Failed to deinitialize GPIO for button %u",
                       config().buttons[i].button);
                ret.combine(deinitRet);
            }
        }
        DESTROY_QUEUE(ret, _eventQueue);
        return ret;
    }

    ReturnCode _onTaskStep() {
        auto ret = OK();
        GpioEvent event{};
        while (Totem::Queue::Platform::receive(_eventQueue, &event, 0).ok()) {
            ret.combine(_publishGpioEvent(event));
        }
        ret.combine(_pollGpioLevels());
        return ret;
    }

    ReturnCode _publishGpioEvent(const GpioEvent &event) {
        const auto btnIndex = _buttonIndexForPin(event.pin);
        FAIL_IF(!btnIndex.has_value(), ERR(NotFound),
                "Received GPIO event for unconfigured pin %u", event.pin);
        const auto index = *btnIndex;
        const auto &btnCfg = config().buttons[index];

        if (_levelsKnown[index] && _lastLevels[index] == event.level) {
            metrics().addDuplicate();
            return OK();
        }

        _lastLevels[index] = event.level;
        _levelsKnown[index] = true;

        const bool pressed = event.level != btnCfg.activeLow;
        if ((pressed && !btnCfg.notifyPressed) ||
            (!pressed && !btnCfg.notifyReleased)) {
            metrics().addIgnored();
            return OK();
        }

        auto pubSubEvent = ButtonEvent{
            .type = pressed ? ButtonEventType::Pressed
                            : ButtonEventType::Released,
            .button = btnCfg.button,
        };
        auto messageIdResult = _messagePool.store(pubSubEvent);
        if (!messageIdResult) {
            metrics().addPublishFailure();
            FAIL_ERR_FWD(messageIdResult.error(),
                         "Failed to store button event in PubSub pool");
        }
        const auto messageId = *messageIdResult;
        _log_i("Button event: button=%u type=%s messageId=%u",
               pubSubEvent.button,
               pubSubEvent.type == ButtonEventType::Pressed ? "Pressed"
                                                            : "Released",
               messageId);

        auto envelopeDef = Totem::PubSubBackend::EnvelopeDef{
            .owner = static_cast<void *>(&_messagePool),
            .topic = NodeData::PubSub::Topic::Button,
            .messageId = messageId,
            .source = static_cast<Totem::PubSubBackend::NodeId>(
                PubSubService::get().nodeId()),
            .getPayloadPtr = MessagePool::getPtr,
            .encodePayload = MessagePool::encodePayload,
            .release = MessagePool::release,
            .requireSyncedClock = false,
        };

        auto envelopeResult =
            Totem::PubSubBackend::Envelope::make<ButtonEvent>(envelopeDef);
        if (!envelopeResult) {
            metrics().addPublishFailure();
            if (!_messagePool.release({.header = {.messageId = messageId}})
                     .ok()) {
                _log_e("Failed to release button event from PubSub pool after "
                       "envelope creation failure");
            }
            FAIL_ERR_FWD(envelopeResult.error(),
                         "Failed to create PubSub envelope for button event");
        }

        auto publishResult = PubSubService::get().publish(*envelopeResult);
        if (!publishResult) {
            metrics().addPublishFailure();
            if (!_messagePool.release({.header = {.messageId = messageId}})) {
                _log_e("Failed to release button event from PubSub pool after "
                       "publish failure");
            }
            FAIL_ERR_FWD(publishResult,
                         "Failed to publish button event envelope to PubSub");
        }

        metrics().addPublished();
        return OK();
    }

    ReturnCode _pollGpioLevels() {
        auto ret = OK();

        for (size_t i = 0; i < config().buttons.size(); ++i) {
            auto levelResult = _gpios[i].level();
            if (!levelResult) {
                ret.combine(levelResult.error());
                _log_e("Failed to poll GPIO level for button %u",
                       config().buttons[i].button);
                continue;
            }

            const bool level = *levelResult;
            if (!_levelsKnown[i]) {
                _lastLevels[i] = level;
                _levelsKnown[i] = true;
                continue;
            }

            if (_lastLevels[i] == level) {
                continue;
            }

            _log_v("Polled button " SV_FMT " level change on pin " SV_FMT
                   ": level=%u",
                   MAGIC_SV_ARG(config().buttons[i].button),
                   MAGIC_SV_ARG(config().buttons[i].pin), level ? 1 : 0);
            metrics().addPollChange();

            ret.combine(_publishGpioEvent(
                {.pin = config().buttons[i].pin,
                 .type = level ? GpioEventType::Rising
                               : GpioEventType::Falling,
                 .level = level,
                 .timestampUs = ::platform::get_time_us()}));
        }

        return ret;
    }

    [[nodiscard]] std::optional<size_t> _buttonIndexForPin(Pin pin) const {
        for (size_t i = 0; i < config().buttons.size(); ++i) {
            if (config().buttons[i].pin == pin) {
                return i;
            }
        }
        return std::nullopt;
    }

    static void _isrCallback(void *ctx, GpioEvent event) {
        auto *buttons = static_cast<Buttons *>(ctx);
        buttons->_handleGpioEvent(event);
    }

    void _handleGpioEvent(GpioEvent event) {
        if (_eventQueue == nullptr) {
            return;
        }
        metrics().addIsrEvent();
        if (!Totem::Queue::Platform::sendFromIsr(_eventQueue, &event)) {
            metrics().addIsrDrop();
            return;
        }
        Totem::TaskController::Controller::signalTaskFromIsr(_task,
                                                             Signal::Ping);
    }

    Totem::TaskController::RunnerKey _task = 0;

    STANDARD_QUEUE(_eventQueue, GpioEvent, ButtonConfig::eventQueueSize)

    std::array<::platform::Gpio, ButtonConfig::maxButtons> _gpios;
    std::array<bool, ButtonConfig::maxButtons> _lastLevels{};
    std::array<bool, ButtonConfig::maxButtons> _levelsKnown{};

    MessagePool _messagePool;

    static constexpr LogComponent logComponent =
        Totem::Buttons::detail::logComponent;
};

inline constexpr LifecycleContract<Buttons, Config> _buttons_lifecycle_contract;
inline constexpr TaskControllerContract<Buttons>
    _buttons_task_controller_contract;
inline constexpr TaskController::TaskHooks::Contract<Buttons>
    _buttons_task_hooks_contract;

} // namespace Totem::Buttons::detail
