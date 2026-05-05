#pragma once

#ifndef FASTLED_FORCE_NAMESPACE
#define FASTLED_FORCE_NAMESPACE 1
#endif

#ifndef FASTLED_NO_PINMAP
#define FASTLED_NO_PINMAP 1
#endif

#ifndef FASTLED_ESP32_I2S_SUPPORTED
#define FASTLED_ESP32_I2S_SUPPORTED 0
#endif

#ifndef F_CPU
#include "sdkconfig.h"
#if defined(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)
#define F_CPU (CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000L)
#else
#define F_CPU 240000000L
#endif
#endif

#ifndef INPUT
#define INPUT 0x0
#endif

#ifndef OUTPUT
#define OUTPUT 0x1
#endif

#ifndef INPUT_PULLUP
#define INPUT_PULLUP 0x2
#endif

#ifndef LOW
#define LOW 0x0
#endif

#ifndef HIGH
#define HIGH 0x1
#endif

#include <stdint.h>

#ifdef __cplusplus
#if __has_include("esp_idf_version.h")
#include "esp_idf_version.h"
#endif

#if __has_include("driver/gpio.h")
#include "driver/gpio.h"
#define TOTEM_FASTLED_HAS_GPIO 1
#else
#define TOTEM_FASTLED_HAS_GPIO 0
#endif

#if __has_include("esp_timer.h")
#include "esp_timer.h"
#define TOTEM_FASTLED_HAS_ESP_TIMER 1
#else
#define TOTEM_FASTLED_HAS_ESP_TIMER 0
#endif

#if __has_include("soc/gpio_reg.h")
#include "soc/gpio_reg.h"
#endif

#if __has_include("freertos/FreeRTOS.h") && __has_include("freertos/task.h")
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define TOTEM_FASTLED_HAS_FREERTOS 1
#else
#define TOTEM_FASTLED_HAS_FREERTOS 0
#endif

static inline unsigned long millis() noexcept {
#if TOTEM_FASTLED_HAS_ESP_TIMER
    return static_cast<unsigned long>(esp_timer_get_time() / 1000LL);
#else
    return 0UL;
#endif
}

static inline unsigned long micros() noexcept {
#if TOTEM_FASTLED_HAS_ESP_TIMER
    return static_cast<unsigned long>(esp_timer_get_time());
#else
    return 0UL;
#endif
}

static inline void pinMode(uint8_t pin, uint8_t mode) noexcept {
#if TOTEM_FASTLED_HAS_GPIO
    const auto direction = mode == OUTPUT ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    (void)gpio_set_direction(static_cast<gpio_num_t>(pin), direction);
    if (mode == INPUT_PULLUP) {
        (void)gpio_pullup_en(static_cast<gpio_num_t>(pin));
    }
#else
    (void)pin;
    (void)mode;
#endif
}

static inline int digitalRead(uint8_t pin) noexcept {
#if TOTEM_FASTLED_HAS_GPIO
    return gpio_get_level(static_cast<gpio_num_t>(pin)) ? HIGH : LOW;
#else
    (void)pin;
    return LOW;
#endif
}

static inline void digitalWrite(uint8_t pin, uint8_t value) noexcept {
#if TOTEM_FASTLED_HAS_GPIO
    (void)gpio_set_level(static_cast<gpio_num_t>(pin),
                         value == LOW ? 0 : 1);
#else
    (void)pin;
    (void)value;
#endif
}

static inline void delay(unsigned long ms) noexcept {
#if TOTEM_FASTLED_HAS_FREERTOS
    vTaskDelay(pdMS_TO_TICKS(ms));
#else
    (void)ms;
#endif
}

static inline void yield() noexcept {
#if TOTEM_FASTLED_HAS_FREERTOS
    taskYIELD();
#endif
}
#endif
