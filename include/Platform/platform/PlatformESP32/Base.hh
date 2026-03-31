#pragma once

#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/ringbuf.h"

namespace platform {

using Tick = TickType_t;
using TaskHandle = TaskHandle_t;

inline Tick get_tick() { return xTaskGetTickCount(); }
inline Tick ms_to_ticks(uint32_t millis) { return pdMS_TO_TICKS(millis); }
inline uint32_t ticks_to_ms(Tick ticks) { return ticks * portTICK_PERIOD_MS; }

using RingBufferHandle = RingbufHandle_t;

} // namespace platform
