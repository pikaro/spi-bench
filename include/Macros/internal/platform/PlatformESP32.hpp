// IWYU pragma: private

#pragma once

#define TICKS_TO_MS(xTicks) ((xTicks * 1000) / configTICK_RATE_HZ)
#define MS_MAX_DELAY TICKS_TO_MS(portMAX_DELAY)
#define TICK_MAX_DELAY portMAX_DELAY
