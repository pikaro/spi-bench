#pragma once

#include "Common.hh"

#include "TaskController/detail/Config.hh"
#include "TaskController/detail/Types.hh"
#include "TaskController/detail/platform/PlatformCommon.hh"
#include "Types/Signal.hh"
#include "esp_task_wdt.h"
#include "freertos/idf_additions.h"
#include <cstdint>
#include <expected>

namespace Totem::TaskController::detail::platform {

#define NOTIFY_ENTRY_CLEAR_ALL 0
#define NOTIFY_EXIT_CLEAR_ALL 0xFFFFFFFFU

using PlatformResultCreateTask = PlatformResultCreateTaskT<TaskHandle_t>;

struct Platform {
    using TaskHandle = ::platform::TaskHandle;
    using TaskStatus = ::platform::TaskStatus;
    using StackDepth = ::platform::StackDepth;

    static PlatformResultCreateTask create_task(Config config,
                                                TaskFunction_t taskFunction,
                                                void *taskParameter) {
        auto affinity = (config.core.kind == Config::CorePreference::Kind::Any)
                            ? tskNO_AFFINITY
                            : config.core.core;

        TaskHandle_t handle = nullptr;

        auto priority = static_cast<UBaseType_t>(config.priority);

        // configSTACK_DEPTH_TYPE == uint32_t, which is already the type of
        // stackSize

        BaseType_t result;
        if (affinity == tskNO_AFFINITY) {
            result = xTaskCreate(taskFunction, config.name, config.stackSize,
                                 taskParameter, priority, &handle);
        } else {
            result = xTaskCreatePinnedToCore(
                taskFunction, config.name, config.stackSize, taskParameter,
                priority, &handle, config.core.core);
        }

        if (result != pdPASS || handle == nullptr) {
            return PlatformResultCreateTask{
                .ok = false,
                .handle = nullptr,
            };
        }

        return PlatformResultCreateTask{
            .ok = true,
            .handle = handle,
        };
    }

    static void delete_task(TaskHandle task) { vTaskDelete(task); }
    static void delete_current_task() { vTaskDelete(nullptr); }
    static void kill_task(TaskHandle task) { delete_task(task); }

    static ReturnCode signal_task(TaskHandle task,
                                  Signal signal = Signal::Ping) {
        auto result = xTaskNotify(task, static_cast<uint32_t>(signal),
                                  eSetValueWithOverwrite);
        FAIL_IF(result != pdPASS, ERR(CoreError, OperationFailed),
                "Failed to signal task");
        return OK(CoreError);
    }

    static bool is_alive(TaskHandle task) {
        return task != nullptr && eTaskGetState(task) != eDeleted;
    }

    static SignalWaitResult wait_for_signal(const char *ctx,
                                            uint32_t timeoutMs = MS_MAX_DELAY,
                                            bool expectTimeout = true) {
        (void)xTaskNotifyStateClear(nullptr);
        uint32_t notifyValue = 0;
        auto ret =
            xTaskNotifyWait(NOTIFY_ENTRY_CLEAR_ALL, NOTIFY_EXIT_CLEAR_ALL,
                            &notifyValue, pdMS_TO_TICKS(timeoutMs));

        if (ret == pdTRUE) {
            return {
                .signal = static_cast<Signal>(notifyValue),
                .timeout = false,
                .ok = true,
            };
        }

        if (expectTimeout) {
            return {
                .signal = Signal::Unknown,
                .timeout = true,
                .ok = true,
            };
        }

        _log_e("Wait for signal in context %s failed: timeout", ctx);
        return {
            .signal = Signal::Unknown,
            .timeout = true,
            .ok = false,
        };
    }

    static void wdt_reset() { esp_task_wdt_reset(); }
    static ReturnCode wdt_add() {
        auto result = esp_task_wdt_add(nullptr);
        FAIL_IF_ESP(result, ERR(CoreError, OperationFailed),
                    "Failed to add task to watchdog");
        return OK(CoreError);
    }
    static ReturnCode wdt_remove() {
        auto result = esp_task_wdt_delete(nullptr);
        FAIL_IF_ESP(result, ERR(CoreError, OperationFailed),
                    "Failed to remove task from watchdog");
        return OK(CoreError);
    }

    static void delay_task_until(::platform::Tick *lastWakeTime,
                                 ::platform::Tick intervalTicks) {
        xTaskDelayUntil(lastWakeTime, intervalTicks);
    }

    static std::expected<TaskPlatformSnapshot, ReturnCode>
    get_snapshot(TaskHandle taskHandle) {
        TaskStatus_t taskStatus;
        vTaskGetInfo(taskHandle, &taskStatus, pdTRUE, eInvalid);

        TaskPlatformSnapshot snapshot;

        switch (taskStatus.eCurrentState) {
        case eRunning:
            snapshot.state = PlatformState::Running;
            break;
        case eReady:
            snapshot.state = PlatformState::Ready;
            break;
        case eBlocked:
            snapshot.state = PlatformState::Blocked;
            break;
        case eSuspended:
            snapshot.state = PlatformState::Suspended;
            break;
        case eDeleted:
            return std::unexpected(ERR(CoreError, NotFound));
        case eInvalid:
            return std::unexpected(ERR(CoreError, InvalidState));
        }

        snapshot.priority = static_cast<uint8_t>(taskStatus.uxCurrentPriority);
        snapshot.runTimeMs =
            ::platform::ticks_to_ms(taskStatus.ulRunTimeCounter);
        snapshot.stackLowestFree = taskStatus.usStackHighWaterMark;
        // FIXME: xTaskGetInfo doesn't return the core ID?
        snapshot.coreId = 0; // taskStatus.xCoreID;

        return snapshot;
    }
};

} // namespace Totem::TaskController::detail::platform
