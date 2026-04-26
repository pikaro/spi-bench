#pragma once

#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "portmacro.h"
#include <expected>

namespace Totem::Mutex::detail::platform {

struct Platform {
    using MutexHandle = ::platform::MutexHandle;
    using Tick = ::platform::Tick;

    static std::expected<MutexHandle, ReturnCode> create_mutex() {
        auto *handle = xSemaphoreCreateMutex();
        if (handle == nullptr) {
            _log_e("Failed to create mutex");
            return std::unexpected(ERR(CoreError, OperationFailed));
        }
        return handle;
    }

    static ReturnCode destroy_mutex(MutexHandle handle) {
        vSemaphoreDelete(handle);
        return OK(CoreError);
    }

    static ReturnCode take_mutex(MutexHandle handle, Tick timeout) {
        if (xSemaphoreTake(handle, timeout) == pdTRUE) {
            _log_d("Mutex taken by %s", pcTaskGetName(nullptr));
            return OK(CoreError);
        }
        return ERR(CoreError, Timeout);
    }

    static ReturnCode give_mutex(MutexHandle handle) {
        if (xSemaphoreGive(handle) == pdTRUE) {
            return OK(CoreError);
        }
        return ERR(CoreError, OperationFailed);
    }

    [[nodiscard]] static bool can_take_mutex(const char *ctx,
                                             MutexHandle handle) {
        if (handle == nullptr) {
            _log_w("%s: Null mutex handle, skipping take", ctx);
            return false;
        }

        if (xPortInIsrContext() != pdFALSE) {
            _log_w("%s: In ISR context, cannot take mutex", ctx);
            return false;
        }

        return true;
    }

    [[nodiscard]] static const char *current_task_name() {
        return pcTaskGetName(nullptr);
    }

    [[nodiscard]] static const char *mutex_holder_name(MutexHandle handle) {
        auto *holder = xSemaphoreGetMutexHolder(handle);
        return holder != nullptr ? pcTaskGetName(holder) : "<none>";
    }

    [[nodiscard]] static bool held_by_current_task(MutexHandle handle) {
        auto *holder = xSemaphoreGetMutexHolder(handle);
        return holder != nullptr && holder == xTaskGetCurrentTaskHandle();
    }
};

} // namespace Totem::Mutex::detail::platform
