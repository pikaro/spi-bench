// IWYU pragma: private

#pragma once

#include "Macros/Facade.hpp"
#include "Platform/Gpio.hpp"
#include "Platform/Hardware.hpp"
#include "Platform/platform/PlatformESP32/Base.hpp"
#include "Types/Error.hpp"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace Totem::GpioSignalTest::detail::platform {

class Producer {
  public:
    DELETE_COPY(Producer)
    DELETE_MOVE(Producer)

    Producer() = default;

    ReturnCode init(Pin pin, const char *name, uint32_t highTimeUs,
                    uint32_t lowTimeUs) {
        FAIL_IF(_timer != nullptr, ERR(LifecycleError, Active),
                "GPIO signal producer already active");

        auto gpioRet = _gpio.initOutput(pin, GpioOutputMode::PushPull, false);
        FAIL_IF_ERR_FWD(gpioRet, "Failed to initialize GPIO signal output");

        _pin = static_cast<gpio_num_t>(static_cast<uint8_t>(pin));
        _highTimeUs = highTimeUs;
        _lowTimeUs = lowTimeUs;
        _level.store(false, std::memory_order_relaxed);
        _transitions.store(0, std::memory_order_relaxed);
        _timerErrors.store(0, std::memory_order_relaxed);
        _active.store(true, std::memory_order_release);

        const esp_timer_create_args_t timerConfig{
            .callback = _onTimer,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = name,
            .skip_unhandled_events = true,
        };
        auto err = esp_timer_create(&timerConfig, &_timer);
        if (err != ESP_OK) {
            _active.store(false, std::memory_order_release);
            (void)_gpio.deinit();
            FAIL_IF_PLATFORM_FWD(err, "Failed to create GPIO signal timer");
        }

        err = esp_timer_start_once(_timer, _lowTimeUs);
        if (err != ESP_OK) {
            _active.store(false, std::memory_order_release);
            (void)esp_timer_delete(_timer);
            _timer = nullptr;
            (void)_gpio.deinit();
            FAIL_IF_PLATFORM_FWD(err, "Failed to start GPIO signal timer");
        }
        return OK();
    }

    ReturnCode deinit() {
        _active.store(false, std::memory_order_release);
        auto ret = OK();
        if (_timer != nullptr) {
            const auto stopErr = esp_timer_stop(_timer);
            if (stopErr != ESP_OK && stopErr != ESP_ERR_INVALID_STATE) {
                ret.combine(::platform::map_platform_error(stopErr));
            }
            ret.combine(
                ::platform::map_platform_error(esp_timer_delete(_timer)));
            _timer = nullptr;
        }
        ret.combine(_gpio.deinit());
        _pin = GPIO_NUM_NC;
        return ret;
    }

    [[nodiscard]] bool level() const {
        return _level.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint32_t transitions() const {
        return _transitions.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint32_t timerErrors() const {
        return _timerErrors.load(std::memory_order_acquire);
    }

  private:
    static void _onTimer(void *owner) {
        auto *self = static_cast<Producer *>(owner);
        if (self == nullptr || !self->_active.load(std::memory_order_acquire)) {
            return;
        }

        const bool nextLevel = !self->_level.load(std::memory_order_relaxed);
        if (gpio_set_level(self->_pin, nextLevel ? 1 : 0) != ESP_OK) {
            self->_timerErrors.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        self->_level.store(nextLevel, std::memory_order_release);
        self->_transitions.fetch_add(1, std::memory_order_relaxed);

        if (!self->_active.load(std::memory_order_acquire)) {
            return;
        }
        const auto delayUs = nextLevel ? self->_highTimeUs : self->_lowTimeUs;
        if (esp_timer_start_once(self->_timer, delayUs) != ESP_OK) {
            self->_timerErrors.fetch_add(1, std::memory_order_relaxed);
        }
    }

    ::platform::Gpio _gpio;
    esp_timer_handle_t _timer = nullptr;
    gpio_num_t _pin = GPIO_NUM_NC;
    uint32_t _highTimeUs = 0;
    uint32_t _lowTimeUs = 0;
    std::atomic<uint32_t> _transitions{0};
    std::atomic<uint32_t> _timerErrors{0};
    std::atomic<bool> _active{false};
    std::atomic<bool> _level{false};
};

inline ReturnCode dumpPinConfiguration(Pin pin) {
    const auto mask = 1ULL << static_cast<uint8_t>(pin);
    FAIL_IF_PLATFORM_FWD(gpio_dump_io_configuration(stdout, mask),
                         "Failed to dump GPIO signal pin configuration");
    return OK();
}

} // namespace Totem::GpioSignalTest::detail::platform
