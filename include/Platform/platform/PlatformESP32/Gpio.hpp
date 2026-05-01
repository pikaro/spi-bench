// IWYU pragma: private

#pragma once

#include "Macros/Facade.hpp"
#include "Platform/Hardware.hpp"
#include "Platform/platform/PlatformESP32/Base.hpp"
#include "Types/Error.hpp"
#include "Types/Gpio.hpp"
#include "driver/gpio.h"
#include "esp_err.h"
#include "hal/gpio_types.h"
#include "soc/gpio_num.h"
#include <cstdint>
#include <expected>
#include <optional>

namespace platform {

class Gpio {
  public:
    DELETE_COPY(Gpio)
    DELETE_MOVE(Gpio)

    Gpio() = default;

    ReturnCode initInput(Pin pin, GpioPull pull = GpioPull::None,
                         GpioInterrupt interrupt = GpioInterrupt::Disabled) {
        _pin = pin;
        _interrupt = interrupt;

        const gpio_config_t config{
            .pin_bit_mask = _pinMask(pin),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en =
                pull == GpioPull::Up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = pull == GpioPull::Down ? GPIO_PULLDOWN_ENABLE
                                                   : GPIO_PULLDOWN_DISABLE,
            .intr_type = _toInterruptType(interrupt),
        };
        FAIL_IF_PLATFORM_FWD(gpio_config(&config),
                             "Failed to configure GPIO input");
        return OK();
    }

    ReturnCode initOutput(Pin pin,
                          GpioOutputMode mode = GpioOutputMode::PushPull,
                          bool initialLevel = false) {
        _pin = pin;
        _interrupt = GpioInterrupt::Disabled;

        const gpio_config_t config{
            .pin_bit_mask = _pinMask(pin),
            .mode = mode == GpioOutputMode::OpenDrain ? GPIO_MODE_OUTPUT_OD
                                                      : GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        FAIL_IF_PLATFORM_FWD(gpio_config(&config),
                             "Failed to configure GPIO output");
        return setLevel(initialLevel);
    }

    ReturnCode setLevel(bool high) {
        FAIL_IF(!_pin.has_value(), ERR(CoreError, InvalidState),
                "Cannot set unconfigured GPIO");
        FAIL_IF_PLATFORM_FWD(gpio_set_level(_gpio(), high ? 1 : 0),
                             "Failed to set GPIO");
        return OK();
    }

    std::expected<bool, ReturnCode> level() const {
        FAIL_IF(!_pin.has_value(),
                std::unexpected(ERR(CoreError, InvalidState)),
                "Cannot read unconfigured GPIO");
        return gpio_get_level(_gpio()) != 0;
    }

    ReturnCode registerIsr(void *owner, GpioIsrCallback callback) {
        FAIL_IF(!_pin.has_value(), ERR(CoreError, InvalidState),
                "Cannot register ISR for unconfigured GPIO");
        FAIL_IF(_interrupt == GpioInterrupt::Disabled,
                ERR(CoreError, InvalidState),
                "Cannot register ISR without GPIO interrupt");
        FAIL_IF(callback == nullptr, ERR(CoreError, InvalidArgument),
                "Invalid GPIO ISR callback");

        _owner = owner;
        _isrCallback = callback;

        const auto installRet = gpio_install_isr_service(0);
        FAIL_IF(installRet != ESP_OK && installRet != ESP_ERR_INVALID_STATE,
                ERR(CoreError, OperationFailed),
                "Failed to install GPIO ISR service");
        FAIL_IF_PLATFORM_FWD(gpio_isr_handler_add(_gpio(), _isr, this),
                             "Failed to register GPIO ISR handler");
        _isrRegistered = true;
        return OK();
    }

    ReturnCode deinit() {
        if (!_pin.has_value()) {
            return OK();
        }
        if (_isrRegistered) {
            (void)gpio_isr_handler_remove(_gpio());
            _isrRegistered = false;
        }
        _owner = nullptr;
        _isrCallback = nullptr;
        _interrupt = GpioInterrupt::Disabled;
        _pin.reset();
        return OK();
    }

    [[nodiscard]] bool configured() const { return _pin.has_value(); }

  private:
    static void _isr(void *arg) {
        auto *self = static_cast<Gpio *>(arg);
        if (self == nullptr || self->_isrCallback == nullptr ||
            !self->_pin.has_value()) {
            return;
        }
        const auto timestampUs = ::platform::get_time_us();
        const bool level = gpio_get_level(self->_gpio()) != 0;
        self->_isrCallback(self->_owner,
                           GpioEvent{.pin = *self->_pin,
                                     .type = level ? GpioEventType::Rising
                                                   : GpioEventType::Falling,
                                     .level = level,
                                     .timestampUs = timestampUs});
    }

    [[nodiscard]] static uint64_t _pinMask(Pin pin) {
        return 1ULL << static_cast<uint8_t>(pin);
    }

    [[nodiscard]] gpio_num_t _gpio() const {
        return static_cast<gpio_num_t>(static_cast<uint8_t>(*_pin));
    }

    [[nodiscard]] static gpio_int_type_t
    _toInterruptType(GpioInterrupt interrupt) {
        switch (interrupt) {
        case GpioInterrupt::Rising:
            return GPIO_INTR_POSEDGE;
        case GpioInterrupt::Falling:
            return GPIO_INTR_NEGEDGE;
        case GpioInterrupt::AnyEdge:
            return GPIO_INTR_ANYEDGE;
        case GpioInterrupt::Disabled:
        default:
            return GPIO_INTR_DISABLE;
        }
    }

    std::optional<Pin> _pin = std::nullopt;
    GpioInterrupt _interrupt = GpioInterrupt::Disabled;
    void *_owner = nullptr;
    GpioIsrCallback _isrCallback = nullptr;
    bool _isrRegistered = false;
};

} // namespace platform
