# Button ISR Metrics Static Guard Pitfall

A 2026 IO-node interrupt watchdog during bell button handling decoded to `vListInsert -> vTaskPlaceOnEventList -> xQueueSemaphoreTake -> __cxa_guard_acquire`, called from `Totem::Buttons::detail::metrics()` inside `Buttons::_handleGpioEvent()` in the GPIO ISR path.

Do not call `metrics()` or other function-local-static/service-backed helpers from GPIO ISRs. Even if the eventual metric increment is cheap or compiled out, the function-local static guard can use FreeRTOS semaphore machinery and is not ISR-safe. For button ISR counters, use local relaxed atomics in the ISR and flush them from the button task context.