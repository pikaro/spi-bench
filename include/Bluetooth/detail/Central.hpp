#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "Bluetooth/Interfaces/Config.hpp"
#include "Bluetooth/Interfaces/Types.hpp"
#include "Bluetooth/detail/Metrics.hpp"
#include "Bluetooth/detail/PlatformSelect.hpp"
#include "Bluetooth/detail/Types.hpp"
#include "Macros/Facade.hpp"
#include "Queue/Facade.hpp" // IWYU pragma: keep
#include "StaticConfig/Bluetooth.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "TaskController/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include "Types/Signal.hpp"
#include <cstddef>
#include <span>

namespace Totem::Bluetooth::detail {

class Central : public HasLifecycle<Central, Config>,
                public HasTaskController<Central, Config> {
    friend class HasLifecycle<Central, Config>;
    friend struct LifecycleContract<Central, Config>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Central, Config>;
    friend struct TaskController::TaskHooks::Contract<Central>;
    friend struct TaskControllerContract<Central>;

  public:
    explicit Central(TaskController::IRegistry &registry)
        : HasTaskController<Central, Config>(registry) {}

    DELETE_COPY(Central)
    DELETE_MOVE(Central)

    static constexpr const char *name = "Bluetooth::Central";

  private:
    ReturnCode _onBegin() {
        if (!config().enabled) {
            _log_i("Bluetooth central disabled by config");
            return OK();
        }

        (void)metrics();
        DEFAULT_TASK();
        _task = task;
        INIT_QUEUE_OR_FAIL(_notificationQueue);
        START_TASK();

        auto platformRet =
            _platform.begin(config(), NotificationSinkBinding{
                                          .owner = this,
                                          .callback = _enqueueNotification,
                                      });
        if (!platformRet.ok()) {
            auto ret = platformRet;
            ret.combine(this->_endTaskController());
            _task = 0;
            DESTROY_QUEUE(ret, _notificationQueue);
            FAIL_ERR_FWD(platformRet, "Failed to begin Bluetooth platform");
        }

        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        ret.combine(_platform.end());
        if (_task != 0) {
            ret.combine(this->_endTaskController());
            _task = 0;
        }
        DESTROY_QUEUE(ret, _notificationQueue);
        return ret;
    }

    static ReturnCode _onTaskNotify(Signal /*signal*/) { return OK(); }

    ReturnCode _onTaskStep() {
        QueuedNotification queued{};
        while (Totem::Queue::Platform::receive(_notificationQueue, &queued, 0)
                   .ok()) {
            if (queued.driver == nullptr) {
                metrics().addDrop();
                continue;
            }

            auto payload = std::span<const std::byte>{
                queued.payload.data(), queued.payloadSize};
            queued.driver->onNotification(Notification{
                .connection = queued.connection,
                .attributeHandle = queued.attributeHandle,
                .indication = queued.indication,
                .payload = payload,
            });
        }
        return OK();
    }

    static ReturnCode
    _enqueueNotification(void *owner, const QueuedNotification &notification) {
        auto *self = static_cast<Central *>(owner);
        FAIL_IF_NULL(self, ERR(InvalidArgument),
                     "Bluetooth notification queue owner is null");
        auto ret =
            Totem::Queue::Platform::send(self->_notificationQueue,
                                         &notification, 0);
        if (!ret.ok()) {
            metrics().addDrop();
            FAIL_ERR_FWD(ret, "Failed to enqueue Bluetooth notification");
        }
        auto signalRet =
            self->_taskController.signalTaskDirect(self->_task, Signal::Ping);
        if (!signalRet.ok()) {
            metrics().addFail();
            FAIL_ERR_FWD(signalRet,
                         "Failed to signal Bluetooth notification task");
        }
        return OK();
    }

    SelectedPlatform _platform{};
    TaskController::RunnerKey _task = 0;

    STANDARD_QUEUE(_notificationQueue, QueuedNotification,
                   StaticConfig::Bluetooth::notificationQueueSize)

    static constexpr LogComponent logComponent = LogComponent::System;
};

inline constexpr LifecycleContract<Central, Config>
    _bluetooth_central_lifecycle_contract;
inline constexpr TaskControllerContract<Central>
    _bluetooth_central_task_controller_contract;
inline constexpr TaskController::TaskHooks::Contract<Central>
    _bluetooth_central_task_hooks_contract;

} // namespace Totem::Bluetooth::detail
