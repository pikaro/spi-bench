#pragma once

#include "Macros/Facade.hpp"
#include "Platform/Hardware.hpp"
#include "Platform/PlatformSelect.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "StatusLed/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include "driver/gpio.h"
#include "esp_err.h"
#include "soc/gpio_num.h"
#include <cstdint>

namespace Totem::StatusLed::detail::platform {

class SplitRgbGpio {
  public:
    DELETE_COPY(SplitRgbGpio)
    DELETE_MOVE(SplitRgbGpio)

    SplitRgbGpio() = default;

    ReturnCode begin(const SplitRgbGpioConfig &config) {
        if (_active) {
            return ERR(LifecycleError, Active);
        }

        FAIL_IF_NOT(config.validate(), ERR(InvalidArgument),
                    "Status LED RGB pins must be distinct");
        FAIL_IF_NOT(isValidPin(config.red) && isValidPin(config.green) &&
                        isValidPin(config.blue),
                    ERR(InvalidArgument),
                    "Invalid status LED RGB pins red=%u green=%u blue=%u",
                    static_cast<unsigned>(config.red),
                    static_cast<unsigned>(config.green),
                    static_cast<unsigned>(config.blue));

        const gpio_config_t gpioConfig{
            .pin_bit_mask = pinMask(config.red) | pinMask(config.green) |
                            pinMask(config.blue),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        FAIL_IF_PLATFORM_FWD(gpio_config(&gpioConfig),
                             "Failed to configure status LED RGB pins");

        _config = config;
        _active = true;
        FAIL_IF_ERR_FWD(show({}), "Failed to clear status LED RGB pins");
        return OK();
    }

    ReturnCode show(RgbColor color) {
        if (!_active) {
            return OK();
        }
        if (_hasLastColor && _lastColor == color) {
            return OK();
        }

        FAIL_IF_PLATFORM_FWD(gpio_set_level(gpio(_config.red),
                                            levelFor(color.red)),
                             "Failed to set status LED red channel");
        FAIL_IF_PLATFORM_FWD(gpio_set_level(gpio(_config.green),
                                            levelFor(color.green)),
                             "Failed to set status LED green channel");
        FAIL_IF_PLATFORM_FWD(gpio_set_level(gpio(_config.blue),
                                            levelFor(color.blue)),
                             "Failed to set status LED blue channel");

        _lastColor = color;
        _hasLastColor = true;
        return OK();
    }

    ReturnCode deinit() {
        if (!_active) {
            return OK();
        }

        auto ret = show({});
        ret.combine(
            ::platform::map_platform_error(
                gpio_reset_pin(gpio(_config.red))));
        ret.combine(
            ::platform::map_platform_error(
                gpio_reset_pin(gpio(_config.green))));
        ret.combine(
            ::platform::map_platform_error(
                gpio_reset_pin(gpio(_config.blue))));

        _active = false;
        _hasLastColor = false;
        return ret;
    }

    [[nodiscard]] static bool isValidPin(Pin pin) {
        return GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(pin));
    }

  private:
    [[nodiscard]] static gpio_num_t gpio(Pin pin) {
        return static_cast<gpio_num_t>(pin);
    }

    [[nodiscard]] static uint64_t pinMask(Pin pin) {
        return uint64_t{1} << static_cast<uint8_t>(pin);
    }

    [[nodiscard]] int levelFor(uint8_t value) const {
        const bool active = value != 0;
        return active == _config.activeHigh ? 1 : 0;
    }

    SplitRgbGpioConfig _config{};
    RgbColor _lastColor{};
    bool _hasLastColor = false;
    bool _active = false;
};

} // namespace Totem::StatusLed::detail::platform
