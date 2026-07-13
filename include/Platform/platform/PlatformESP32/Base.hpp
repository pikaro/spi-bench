// IWYU pragma: private

#pragma once

#include "FreeRTOSConfig.h"
#include "Macros/internal/Error.hpp"
#include "Types/Error.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "nvs_flash.h"
#include "portmacro.h"
#include <cstdint>

namespace platform {

using Tick = TickType_t;
using TaskHandle = TaskHandle_t;
using TaskFunction = TaskFunction_t;

inline ReturnCode init() {
    nvs_flash_init();
    return OK();
}

inline Tick ms_to_ticks(uint32_t millis) { return pdMS_TO_TICKS(millis); }
inline uint32_t ticks_to_ms(Tick ticks) { return ticks * portTICK_PERIOD_MS; }

inline Tick get_tick() { return xTaskGetTickCount(); }
inline uint32_t get_time() { return ticks_to_ms(get_tick()); }
inline int64_t get_time_us() { return esp_timer_get_time(); }

inline void delay(Tick ticks) { vTaskDelay(ticks); }
inline void delay_until(Tick *lastWakeTime, Tick ticks) {
    vTaskDelayUntil(lastWakeTime, ticks);
}

using RingBufferHandle = RingbufHandle_t;
using QueueHandle = QueueHandle_t;
using MutexHandle = SemaphoreHandle_t;

using TaskStatus = TaskStatus_t;
using StackDepth = configSTACK_DEPTH_TYPE;

inline constexpr auto MaxTaskNameLen = configMAX_TASK_NAME_LEN;

inline bool in_isr() { return xPortInIsrContext() != pdFALSE; }

using Spinlock = portMUX_TYPE;

inline Spinlock create_spinlock() { return portMUX_INITIALIZER_UNLOCKED; }

inline void start_critical_section(Spinlock &lock) {
    portENTER_CRITICAL(&lock);
}
inline void end_critical_section(Spinlock &lock) { portEXIT_CRITICAL(&lock); }

inline static constexpr auto CoreCount = portNUM_PROCESSORS;

inline bool is_multithreading() {
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

inline void wait_for_ready() {
    while (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        delay(ms_to_ticks(10));
    }
}

inline void early_log_error(const char *tag, const char *msg) {
    ESP_EARLY_LOGE(tag, "%s", msg);
}

inline void early_log_warning(const char *tag, const char *msg) {
    ESP_EARLY_LOGW(tag, "%s", msg);
}

inline void early_log_info(const char *tag, const char *msg) {
    ESP_EARLY_LOGI(tag, "%s", msg);
}

inline void early_log_debug(const char *tag, const char *msg) {
    ESP_EARLY_LOGD(tag, "%s", msg);
}

inline void early_log_verbose(const char *tag, const char *msg) {
    ESP_EARLY_LOGV(tag, "%s", msg);
}

[[nodiscard]] inline const char *current_task_name() {
    if (xPortInIsrContext() != pdFALSE) {
        return "<ISR>";
    }
    if (xTaskGetCurrentTaskHandle() == nullptr) {
        return "<none>";
    }
    if (pcTaskGetName(nullptr) == nullptr) {
        return "<unnamed>";
    }
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return "<static>";
    }
    return pcTaskGetName(nullptr);
}

constexpr ReturnCode map_platform_error_core(esp_err_t err) {
    switch (err) {
    case ESP_OK:
        return OK();
    case ESP_FAIL:
        return ERR(CoreError, OperationFailed);
    case ESP_ERR_INVALID_ARG:
        return ERR(CoreError, InvalidArgument);
    case ESP_ERR_INVALID_STATE:
        return ERR(CoreError, InvalidState);
    case ESP_ERR_INVALID_SIZE:
        return ERR(CoreError, InvalidSize);
    case ESP_ERR_NO_MEM:
        return ERR(CoreError, OutOfMemory);
    case ESP_ERR_NOT_FOUND:
        return ERR(CoreError, NotFound);
    case ESP_ERR_TIMEOUT:
        return ERR(CoreError, Timeout);
    case ESP_ERR_NOT_SUPPORTED:
        return ERR(CoreError, NotSupported);
    case ESP_ERR_INVALID_RESPONSE:
        return ERR(CoreError, InvalidResponse);
    case ESP_ERR_INVALID_CRC:
        return ERR(CoreError, CrcError);
    case ESP_ERR_INVALID_VERSION:
        return ERR(CoreError, InvalidVersion);
    case ESP_ERR_INVALID_MAC:
        return ERR(CoreError, InvalidMac);
    case ESP_ERR_NOT_FINISHED:
        return ERR(CoreError, NotFinished);
    case ESP_ERR_NOT_ALLOWED:
        return ERR(CoreError, Forbidden);
    default:
        return ERR(CoreError, Unknown);
    }
}

constexpr ReturnCode map_platform_error(esp_err_t err) {
    if (err >= -1 && err <= 0x3000) {
        return map_platform_error_core(err);
    }

    // TODO: Map other error ranges (WiFi, flash, etc.) as needed

    return ERR(CoreError, Unknown);
}

} // namespace platform
