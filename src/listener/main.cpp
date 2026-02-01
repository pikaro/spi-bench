#include "freertos/FreeRTOS.h"
#include "models.hh"

void setup() {}

extern "C" {
void app_main(void);
}

TickType_t lastWakeTime;

void app_main() {
    setup();
    for (;;) {
        xTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(100));
    }
}
