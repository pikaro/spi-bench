#pragma once

#include "Common.hh"
#include "freertos/idf_additions.h"
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
            return OK(CoreError);
        }
        return ERR(CoreError, OperationFailed);
    }

    static ReturnCode give_mutex(MutexHandle handle) {
        if (xSemaphoreGive(handle) == pdTRUE) {
            return OK(CoreError);
        }
        return ERR(CoreError, OperationFailed);
    }
};

} // namespace Totem::Mutex::detail::platform
