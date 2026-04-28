#pragma once

#include "FreeRTOSConfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "portmacro.h"
#include <cstdint>

namespace platform {

using Tick = TickType_t;
using TaskHandle = TaskHandle_t;
using TaskFunction = TaskFunction_t;

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

} // namespace platform
