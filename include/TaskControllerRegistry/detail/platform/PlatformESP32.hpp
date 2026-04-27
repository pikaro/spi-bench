#pragma once

#include "FreeRTOSConfig.h"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Support/Basic.hpp"
#include "TaskController/Interfaces/TaskRuntimeSnapshot.hpp"
#include "Types/Error.hpp"
#include "freertos/idf_additions.h"
#include "sdkconfig.h"
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace Totem::TaskControllerRegistry::detail::platform {

struct Platform {
    using TaskStatus = ::platform::TaskStatus;

    static std::expected<size_t, ReturnCode>
    get_system_task_statuses(std::span<TaskStatus> taskStatuses,
                             uint32_t totalRuntimeCounter) {
        return uxTaskGetSystemState(taskStatuses.data(), taskStatuses.size(),
                                    &totalRuntimeCounter);
    }

    static std::expected<uint8_t, ReturnCode> get_task_count() {
        auto ret = uxTaskGetNumberOfTasks();
        FAIL_IF(ret > UINT8_MAX, std::unexpected(ERR(Overflow)),
                "System task count overflow");
        return static_cast<uint8_t>(ret);
    }

    static std::optional<TaskController::PlatformState>
    map_platform_state(TaskStatus status) {
        switch (status.eCurrentState) {
        case eRunning:
            return TaskController::PlatformState::Running;
        case eReady:
            return TaskController::PlatformState::Ready;
        case eBlocked:
            return TaskController::PlatformState::Blocked;
        case eSuspended:
            return TaskController::PlatformState::Suspended;
        case eDeleted:
        case eInvalid:
        default:
            return std::nullopt;
        }
    }

    static uint32_t runtime_counter_to_ms(configRUN_TIME_COUNTER_TYPE value) {
#if CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER
        return static_cast<uint32_t>(value / 1000ULL);
#else
        return ::platform::ticks_to_ms(static_cast<::platform::Tick>(value));
#endif
    }

    static uintptr_t get_handle(const TaskStatus &taskStatus) {
        return reinterpret_cast<uintptr_t>(taskStatus.xHandle);
    }

    static uint32_t get_run_time_ms(const TaskStatus &taskStatus) {
        return runtime_counter_to_ms(taskStatus.ulRunTimeCounter);
    }

    static std::string_view get_task_name(const TaskStatus &taskStatus) {
        auto len =
            bounded_strlen(taskStatus.pcTaskName, ::platform::MaxTaskNameLen);
        return {taskStatus.pcTaskName, len};
    }

    static int8_t get_core_id(const TaskStatus &taskStatus) {
        return static_cast<int8_t>(taskStatus.xCoreID);
    }

    static uint8_t get_priority(const TaskStatus &taskStatus) {
        return static_cast<uint8_t>(taskStatus.uxCurrentPriority);
    }

    static uint32_t get_stack_watermark(const TaskStatus &taskStatus) {
        return static_cast<uint32_t>(taskStatus.usStackHighWaterMark);
    }
};

} // namespace Totem::TaskControllerRegistry::detail::platform
