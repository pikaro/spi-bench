#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "Data/Peripherals.hpp"
#include "LedPwm/Interfaces/Config.hpp"
#include "LedPwm/Interfaces/Types.hpp"
#include "LedPwm/detail/Handler.hpp"
#include "LedPwm/detail/Metrics.hpp"
#include "LedPwm/detail/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Queue/Facade.hpp" // IWYU pragma: keep
#include "StaticConfig/LedPwm.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace Totem::LedPwm::detail {

class LedPwm : public HasLifecycle<LedPwm, Config>,
               public HasTaskController<LedPwm, Config> {
    friend class HasLifecycle<LedPwm, Config>;
    friend struct LifecycleContract<LedPwm, Config>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<LedPwm, Config>;
    friend struct TaskController::TaskHooks::Contract<LedPwm>;
    friend struct TaskControllerContract<LedPwm>;

    struct QueueItem {
        LedHandle led;
        LedCommand cmd;
    };

  public:
    explicit LedPwm(TaskController::IRegistry &registry)
        : HasTaskController<LedPwm, Config>(registry) {}

    DELETE_COPY(LedPwm)
    DELETE_MOVE(LedPwm)

    static constexpr const char *name = "LedPwm";

    std::expected<LedContext, ReturnCode> getLedContext(PeripheralLed led) {
        FAIL_IF_INACTIVE_UNEXPECTED("Cannot get LED context before %s begins",
                                    name);
        for (size_t i = 0; i < this->config().leds.size(); ++i) {
            if (!config().leds[i].configured) {
                continue;
            }
            if (config().leds[i].led == led) {
                return LedContext{
                    .ctx = static_cast<void *>(this),
                    .led = LedHandle{.idx = static_cast<uint8_t>(i)},
                    .command = LedPwm::_command,
                };
            }
        }
        FAIL(std::unexpected(ERR(NotFound)),
             "LED with name " SV_FMT " not found in config", MAGIC_SV_ARG(led));
    }

  private:
    ReturnCode _onBegin() {
        prewarmMetrics();
        DEFAULT_TASK();
        INIT_QUEUE_OR_FAIL(_commandQueue);

        auto ret = OK();
        for (size_t i = 0; i < this->config().leds.size(); ++i) {
            if (!config().leds[i].configured) {
                continue;
            }
            auto handle = LedHandle{.idx = static_cast<uint8_t>(i)};
            ret.combine(_handler.init(config(), handle));
        }

        FAIL_IF_ERR_FWD(ret, "Failed to initialize configured LEDs for %s",
                        name);

        START_TASK();
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        ret.combine(this->_endTaskController());
        for (size_t i = 0; i < this->config().leds.size(); ++i) {
            if (!config().leds[i].configured) {
                continue;
            }
            auto handle = LedHandle{.idx = static_cast<uint8_t>(i)};
            ret.combine(_handler.deinit(handle));
        }
        DESTROY_QUEUE(ret, _commandQueue);
        return ret;
    }

    ReturnCode _onTaskStep() {
        metrics().addTaskStep();
        const auto nowMs = ::platform::get_time();
        QueueItem item{};
        while (true) {
            auto result =
                Totem::Queue::Platform::receive(_commandQueue, &item, 5);
            if (!result.ok()) {
                if (result == ERR(Timeout)) {
                    break;
                }
                FAIL_ERR_FWD(result,
                             "Failed to receive LED command from queue");
            }
            auto handleRet = _handler.handle(item.led, item.cmd, nowMs);
            if (!handleRet.ok()) {
                metrics().addHandleFailure();
                FAIL_ERR_FWD(handleRet, "Failed to handle LED command");
            }
            metrics().addHandled();
        }
        auto stepRet = _handler.step(nowMs);
        if (!stepRet.ok()) {
            metrics().addHandleFailure();
            FAIL_ERR_FWD(stepRet, "Failed to step LED animations");
        }
        return OK();
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static ReturnCode _command(void *ctx, const void *cmd) {
        const auto *cmdPtr = static_cast<const LedCommand *>(cmd);
        auto *ctxPtr = static_cast<LedContext *>(ctx);
        FAIL_IF_NULL(cmdPtr, ERR(InvalidArgument), "LED command is null");
        FAIL_IF_NULL(ctxPtr, ERR(InvalidArgument), "LED context is null");
        auto *self = static_cast<LedPwm *>(ctxPtr->ctx);
        FAIL_IF_NULL(self, ERR(InvalidArgument), "LED context owner is null");
        FAIL_IF_SELF_INACTIVE_ERR("Cannot enqueue LED command before %s begins",
                                  name);
        QueueItem item{
            .led = ctxPtr->led,
            .cmd = *cmdPtr,
        };
        auto ret = Totem::Queue::Platform::send(self->_commandQueue, &item);
        if (!ret.ok()) {
            metrics().addQueueFailure();
            FAIL_ERR_FWD(ret, "Failed to enqueue LED command");
        }
        metrics().addQueued();
        return OK();
    }

    Handler _handler;

    STANDARD_QUEUE(_commandQueue, QueueItem, LedPwmConfig::commandQueueSize)

    static constexpr LogComponent logComponent = LogComponent::LedPwm;
};

inline constexpr LifecycleContract<LedPwm, Config> _led_pwm_lifecycle_contract;
inline constexpr TaskControllerContract<LedPwm>
    _led_pwm_task_controller_contract;
inline constexpr TaskController::TaskHooks::Contract<LedPwm>
    _led_pwm_task_hooks_contract;

} // namespace Totem::LedPwm::detail
