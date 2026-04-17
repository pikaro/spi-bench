#pragma once

#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace Totem::Queue::detail::platform {

template <typename T>
concept IsQueueable = requires(T item) {
    { sizeof(T) } -> std::convertible_to<size_t>;
} && std::is_trivially_copyable_v<T>;

struct Platform {
    template <class T, size_t N>
        requires IsQueueable<T>
    struct Storage {
        std::array<T, N> buffer{};
        size_t itemSize = sizeof(T);
        size_t queueLength = N;
        StaticQueue_t _queueStruct{};
    };

    using QueueHandle = ::platform::QueueHandle;
    using Tick = ::platform::Tick;

    template <class T, size_t N>
        requires IsQueueable<T>
    static std::expected<QueueHandle, ReturnCode>
    create(Storage<T, N> &storage) {
        auto *handle = xQueueCreateStatic(
            storage.queueLength, storage.itemSize,
            reinterpret_cast<uint8_t *>(storage.buffer.data()),
            &storage._queueStruct);
        FAIL_IF_NULL(handle, std::unexpected(ERR(InvalidArgument)),
                     "Failed to create queue: depth and item size must be "
                     "greater than 0");
        return handle;
    }

    static ReturnCode destroy(QueueHandle handle) {
        vQueueDelete(handle);
        return OK();
    }

    static ReturnCode send(QueueHandle handle, const void *item,
                           Tick timeout = TICK_MAX_DELAY) {
        auto result = xQueueSendToBack(handle, item, timeout);
        FAIL_IF(result != pdTRUE, ERR(Timeout),
                "Failed to send item to queue: timeout after %u ticks",
                timeout);
        return OK();
    }

    static ReturnCode receive(QueueHandle handle, void *item,
                              Tick timeout = TICK_MAX_DELAY) {
        auto result = xQueueReceive(handle, item, timeout);
        if (result != pdTRUE) {
            return ERR(Timeout);
        }
        return OK();
    }

    static size_t size(QueueHandle handle) {
        return static_cast<size_t>(uxQueueMessagesWaiting(handle));
    }

    static size_t spacesAvailable(QueueHandle handle) {
        return static_cast<size_t>(uxQueueSpacesAvailable(handle));
    }
};

} // namespace Totem::Queue::detail::platform
