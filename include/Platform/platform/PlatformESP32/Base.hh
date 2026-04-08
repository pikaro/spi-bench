#pragma once

#include "FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/ringbuf.h"
#include <cstdint>

namespace platform {

using Tick = TickType_t;
using TaskHandle = TaskHandle_t;
using TaskFunction = TaskFunction_t;

inline Tick ms_to_ticks(uint32_t millis) { return pdMS_TO_TICKS(millis); }
inline uint32_t ticks_to_ms(Tick ticks) { return ticks * portTICK_PERIOD_MS; }

inline Tick get_tick() { return xTaskGetTickCount(); }
inline uint32_t get_time() { return ticks_to_ms(get_tick()); }

inline void delay(Tick ticks) { vTaskDelay(ticks); }
inline void delay_until(Tick *lastWakeTime, Tick ticks) {
    vTaskDelayUntil(lastWakeTime, ticks);
}

using RingBufferHandle = RingbufHandle_t;
using MutexHandle = SemaphoreHandle_t;

using TaskStatus = TaskStatus_t;
using StackDepth = configSTACK_DEPTH_TYPE;

inline bool in_isr() { return xPortInIsrContext() != pdFALSE; }

} // namespace platform
