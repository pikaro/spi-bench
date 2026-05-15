// IWYU pragma: private

#pragma once

#include "FreeRTOSConfig.h"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "TaskController/Interfaces/TaskRuntimeSnapshot.hpp"
#include "TaskController/detail/platform/PlatformCommon.hpp"
#include "Types/Error.hpp"
#include "Types/Signal.hpp"
#include "esp_task_wdt.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "portmacro.h"
#include "sdkconfig.h"
#include <array>
#include <cstdint>
#include <expected>

namespace Totem::TaskController::detail::platform {

#define NOTIFY_ENTRY_CLEAR_ALL 0
#define NOTIFY_EXIT_CLEAR_ALL 0xFFFFFFFFU

using PlatformResultCreateTask = PlatformResultCreateTaskT<TaskHandle_t>;

template <uint32_t StackSize> struct StaticTaskStorage {
    StaticTaskStorage() = default;
    StaticTaskStorage(const StaticTaskStorage &) = delete;
    StaticTaskStorage &operator=(const StaticTaskStorage &) = delete;
    StaticTaskStorage(StaticTaskStorage &&) = delete;
    StaticTaskStorage &operator=(StaticTaskStorage &&) = delete;

    StaticTask_t controlBlock{};
    alignas(portBYTE_ALIGNMENT) std::array<StackType_t, StackSize> stack{};

    [[nodiscard]] StaticTaskMemory memory() {
        return StaticTaskMemory{
            .controlBlock = &controlBlock,
            .stack = stack.data(),
            .stackSize = StackSize,
        };
    }
};

struct Platform {
    using TaskHandle = ::platform::TaskHandle;
    using TaskStatus = ::platform::TaskStatus;
    using StackDepth = ::platform::StackDepth;
    using TaskFunction = ::platform::TaskFunction;

    static PlatformResultCreateTask
    create_task(Config config, TaskFunction taskFunction, void *taskParameter) {
        auto affinity = (config.core.kind == Config::CorePreference::Kind::Any)
                            ? tskNO_AFFINITY
                            : config.core.core;

        TaskHandle_t handle = nullptr;

        auto priority = static_cast<UBaseType_t>(config.priority);

        if (config.allocation == TaskAllocation::Static) {
            if (!config.staticMemory.validFor(config.stackSize)) {
                _log_e("Static task %s has no storage for stack size %lu",
                       config.name,
                       static_cast<unsigned long>(config.stackSize));
                return PlatformResultCreateTask{
                    .ok = false,
                    .handle = nullptr,
                };
            }

            auto *stack = static_cast<StackType_t *>(config.staticMemory.stack);
            auto *controlBlock =
                static_cast<StaticTask_t *>(config.staticMemory.controlBlock);
            handle = xTaskCreateStaticPinnedToCore(
                taskFunction, config.name, config.stackSize, taskParameter,
                priority, stack, controlBlock, affinity);
            if (handle == nullptr) {
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

        auto result = xTaskCreatePinnedToCore(taskFunction, config.name,
                                             config.stackSize, taskParameter,
                                             priority, &handle, affinity);
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

    static void signal_task_from_isr(TaskHandle task,
                                     Signal signal = Signal::Ping) {
        BaseType_t woke = pdFALSE;
        (void)xTaskNotifyFromISR(task, static_cast<uint32_t>(signal),
                                 eSetValueWithOverwrite, &woke);
        if (woke == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }

    static bool is_alive(TaskHandle task) {
        return task != nullptr && eTaskGetState(task) != eDeleted;
    }

    static SignalWaitResult wait_for_signal(const char *ctx,
                                            uint32_t timeoutMs = MS_MAX_DELAY,
                                            bool expectTimeout = true) {
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
        FAIL_IF_PLATFORM_FWD(result, "Failed to add task to watchdog");
        return OK(CoreError);
    }
    static ReturnCode wdt_remove() {
        auto result = esp_task_wdt_delete(nullptr);
        FAIL_IF_PLATFORM_FWD(result, "Failed to remove task from watchdog");
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
            _runtime_counter_to_ms(taskStatus.ulRunTimeCounter);
        snapshot.stackLowestFree = taskStatus.usStackHighWaterMark;
        snapshot.coreId = static_cast<int8_t>(taskStatus.xCoreID);

        return snapshot;
    }

  private:
    static uint32_t _runtime_counter_to_ms(configRUN_TIME_COUNTER_TYPE value) {
#if CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER
        return static_cast<uint32_t>(value / 1000ULL);
#else
        return ::platform::ticks_to_ms(static_cast<::platform::Tick>(value));
#endif
    }
};

} // namespace Totem::TaskController::detail::platform
