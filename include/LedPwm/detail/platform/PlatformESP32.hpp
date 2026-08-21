// IWYU pragma: private

#pragma once

#include "LedPwm/detail/platform/Config.hpp"
#include "Macros/Facade.hpp"
#include "Mutex/Facade.hpp"
#include "Platform/Hardware.hpp"
#include "Types/Error.hpp"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "freertos/idf_additions.h"
#include "hal/ledc_types.h"
#include "soc/clk_tree_defs.h"
#include "soc/gpio_num.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>

namespace Totem::LedPwm::detail::platform {

struct TimerSlot {
    size_t usedBy = 0;
    uint32_t frequency_hz = 0;
    ledc_timer_bit_t duty_resolution = LEDC_TIMER_10_BIT;
    ledc_timer_t timer = LEDC_TIMER_0;
    ledc_clk_cfg_t clock = LEDC_AUTO_CLK;
};

struct ChannelSlot {
    bool used = false;
    gpio_num_t gpio = GPIO_NUM_NC;
    ledc_channel_t channel = LEDC_CHANNEL_0;
    ledc_timer_t timer = LEDC_TIMER_0;
};

static StaticSemaphore_t _mutexStorage{};
static SemaphoreHandle_t _mutex = xSemaphoreCreateMutexStatic(&_mutexStorage);

struct TimerProvider {
    static constexpr const char *name = "LedPwm::TimerProvider";

    static std::expected<ledc_timer_t, ReturnCode>
    findOrAllocateTimer(const Config &cfg) {
        auto guard = Mutex::ScopedMutexGuard<TimerProvider>(_mutex);
        for (auto &timer : _timers) {
            if (timer.usedBy > 0 && timer.frequency_hz == cfg.frequency &&
                timer.duty_resolution == cfg.resolution &&
                timer.clock == LEDC_AUTO_CLK) {
                timer.usedBy++;
                return timer.timer;
            }
        }

        for (size_t i = 0; i < _timers.size(); ++i) {
            if (_timers[i].usedBy == 0) {
                _timers[i].usedBy = 1;
                _timers[i].timer = static_cast<ledc_timer_t>(i);
                _timers[i].frequency_hz = cfg.frequency;
                _timers[i].duty_resolution =
                    static_cast<ledc_timer_bit_t>(cfg.resolution);
                ledc_timer_config_t timer_cfg = {};
                timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
                timer_cfg.timer_num = _timers[i].timer;
                timer_cfg.duty_resolution =
                    static_cast<ledc_timer_bit_t>(cfg.resolution);
                timer_cfg.freq_hz = cfg.frequency;
                timer_cfg.clk_cfg = LEDC_AUTO_CLK;
                auto err = ledc_timer_config(&timer_cfg);
                if (err != ESP_OK) {
                    _timers[i] = TimerSlot{};
                    FAIL_IF_PLATFORM_FWD_UNEXPECTED(
                        err, "Failed to configure LEDC timer");
                }
                return _timers[i].timer;
            }
        }
        FAIL_ERR_UNEXPECTED(NotFound, "No available LEDC timers");
    }

    static ReturnCode freeTimer(ledc_timer_t timer) {
        auto guard = Mutex::ScopedMutexGuard<TimerProvider>(_mutex);
        for (auto &slot : _timers) {
            if (slot.timer == timer) {
                if (slot.usedBy > 0) {
                    slot.usedBy--;
                    if (slot.usedBy == 0) {
                        ledc_timer_config_t timer_cfg = {};
                        timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
                        timer_cfg.timer_num = slot.timer;
                        timer_cfg.deconfigure = true;
                        auto err = ledc_timer_config(&timer_cfg);
                        if (err != ESP_OK) {
                            FAIL_IF_PLATFORM_FWD(
                                err, "Failed to de-configure LEDC timer");
                        }
                        slot = TimerSlot{};
                    }
                    return OK();
                }
                return ERR(NotFound);
            }
        }
        return ERR(NotFound);
    }

  private:
    static inline std::array<TimerSlot, static_cast<size_t>(LEDC_TIMER_MAX)>
        _timers;
};

struct ChannelProvider {
    static constexpr const char *name = "LedPwm::ChannelProvider";

    static std::expected<ledc_channel_t, ReturnCode> allocateChannel() {
        auto guard = Mutex::ScopedMutexGuard<ChannelProvider>(_mutex);
        for (size_t i = 0; i < _channels.size(); ++i) {
            if (!_channels[i].used) {
                _channels[i].used = true;
                _channels[i].channel = static_cast<ledc_channel_t>(i);
                return _channels[i].channel;
            }
        }
        FAIL_ERR_UNEXPECTED(NotFound, "No available LEDC channels");
    }

    static ReturnCode freeChannel(ledc_channel_t channel) {
        auto guard = Mutex::ScopedMutexGuard<ChannelProvider>(_mutex);
        if (channel < LEDC_CHANNEL_MAX) {
            _channels[static_cast<uint8_t>(channel)] = {};
            return OK();
        }
        return ERR(NotFound);
    }

    static ReturnCode setupChannel(ledc_channel_t channel, gpio_num_t gpio,
                                   ledc_timer_t timer) {
        auto guard = Mutex::ScopedMutexGuard<ChannelProvider>(_mutex);
        if (channel < LEDC_CHANNEL_MAX) {
            _channels[static_cast<uint8_t>(channel)].gpio = gpio;
            _channels[static_cast<uint8_t>(channel)].timer = timer;
            return OK();
        }
        return ERR(NotFound);
    }

    static inline std::array<ChannelSlot, static_cast<size_t>(LEDC_CHANNEL_MAX)>
        _channels;
};

struct Platform {
    ReturnCode init(const Pin pin, const Config &config) {
        FAIL_IF(_active, ERR(LifecycleError, Active),
                "LEDC channel already active");

        FAIL_IF_NOT(isValidConfig(pin, config), ERR(InvalidArgument),
                    "Invalid LEDC configuration: frequency=%u, pin=%d, "
                    "resolution=%u",
                    config.frequency, pin, config.resolution);

        FAIL_IF_UNEXPECTED_FWD(timer,
                               TimerProvider::findOrAllocateTimer(config),
                               "Failed to find or allocate LEDC timer");
        auto channelResult = ChannelProvider::allocateChannel();
        if (!channelResult) {
            REPORT_IF_ERR(
                TimerProvider::freeTimer(timer),
                "Failed to free LEDC timer %d after channel allocation failure",
                timer);
            FAIL(channelResult.error(), "Failed to allocate LEDC channel");
        }
        auto channel = *channelResult;

        ledc_channel_config_t channelConfig = {};
        channelConfig.gpio_num = static_cast<gpio_num_t>(pin);
        channelConfig.speed_mode = LEDC_LOW_SPEED_MODE;
        channelConfig.channel = channel;
        channelConfig.intr_type = LEDC_INTR_DISABLE;
        channelConfig.timer_sel = timer;
        channelConfig.duty = config.initialDuty;
        channelConfig.hpoint = 0;

        auto err = ledc_channel_config(&channelConfig);
        if (err != ESP_OK) {
            // Roll back channel allocation.
            REPORT_IF_ERR(
                ChannelProvider::freeChannel(channel),
                "Failed to free LEDC channel %d after channel config failure",
                channel);
            REPORT_IF_ERR(
                TimerProvider::freeTimer(timer),
                "Failed to free LEDC timer %d after channel config failure",
                timer);
            FAIL_IF_PLATFORM_FWD(err, "Failed to configure LEDC channel");
        }

        _config = config;
        _timer = timer;
        _channel = channel;
        _active = true;

        auto setupRet = ChannelProvider::setupChannel(
            channel, static_cast<gpio_num_t>(pin), channelConfig.timer_sel);
        if (!setupRet.ok()) {
            REPORT_IF_ERR(deinit(),
                          "Failed to clean up LEDC channel %d after provider "
                          "setup failure",
                          channel);
            return setupRet;
        }

        return OK();
    }

    ReturnCode deinit() {
        FAIL_IF(!_active, ERR(LifecycleError, NotActive),
                "LEDC channel not active");

        auto ret = ::platform::map_platform_error(
            ledc_stop(LEDC_LOW_SPEED_MODE, _channel, 0));
        ret.combine(ChannelProvider::freeChannel(_channel));
        ret.combine(TimerProvider::freeTimer(_timer));

        _active = false;
        return ret;
    }

    ReturnCode setDutyScaled(uint32_t duty) {
        FAIL_IF(!_active, ERR(LifecycleError, NotActive),
                "LEDC channel not active");
        uint32_t maxDuty = maxDutyValue();
        auto scaled =
            static_cast<uint32_t>((static_cast<uint64_t>(duty) * maxDuty) /
                                  std::numeric_limits<uint32_t>::max());
        return setDuty(scaled);
    }

    ReturnCode setBrightnessScaled(uint32_t brightness) {
        FAIL_IF(!_active, ERR(LifecycleError, NotActive),
                "LEDC channel not active");
        return setDuty(gammaCorrectedDuty(brightness));
    }

    ReturnCode setDuty(uint32_t duty) {
        FAIL_IF(!_active, ERR(LifecycleError, NotActive),
                "LEDC channel not active");
        FAIL_IF_PLATFORM_FWD(ledc_set_duty(LEDC_LOW_SPEED_MODE, _channel, duty),
                             "Failed to set LEDC duty");
        FAIL_IF_PLATFORM_FWD(ledc_update_duty(LEDC_LOW_SPEED_MODE, _channel),
                             "Failed to update LEDC duty");
        return OK();
    }

    ReturnCode setDuty(float duty) {
        FAIL_IF(!_active, ERR(LifecycleError, NotActive),
                "LEDC channel not active");
        if (duty > 1.0F) {
            duty = 1.0F;
        } else if (duty < 0.0F) {
            duty = 0.0F;
        }
        auto dutyValue =
            static_cast<uint32_t>(duty * static_cast<float>(maxDutyValue()));
        return setDuty(dutyValue);
    }

    ReturnCode stop() {
        FAIL_IF(!_active, ERR(LifecycleError, NotActive),
                "LEDC channel not active");
        FAIL_IF_PLATFORM_FWD(ledc_stop(LEDC_LOW_SPEED_MODE, _channel, 0),
                             "Failed to stop LEDC channel");
        return OK();
    }

    ReturnCode resume() {
        FAIL_IF(!_active, ERR(LifecycleError, NotActive),
                "LEDC channel not active");
        // Starting the channel is effectively just updating the duty to apply
        // the current duty setting.
        FAIL_IF_PLATFORM_FWD(ledc_update_duty(LEDC_LOW_SPEED_MODE, _channel),
                             "Failed to start LEDC channel");
        return OK();
    }

    std::expected<uint32_t, ReturnCode> getDuty() {
        FAIL_IF(!_active, std::unexpected(ERR(LifecycleError, NotActive)),
                "LEDC channel not active");
        return ledc_get_duty(LEDC_LOW_SPEED_MODE, _channel);
    }

    [[nodiscard]] static uint32_t maxDutyValue(uint8_t resolution) {
        return (1UL << static_cast<uint32_t>(resolution)) - 1UL;
    }

    [[nodiscard]] uint32_t maxDutyValue() const {
        return maxDutyValue(_config.resolution);
    }

    [[nodiscard]] uint32_t gammaCorrectedDuty(uint32_t brightness) const {
        if (brightness == 0) {
            return 0;
        }
        if (brightness == std::numeric_limits<uint32_t>::max()) {
            return maxDutyValue();
        }

        const auto normalized =
            static_cast<float>(brightness) /
            static_cast<float>(std::numeric_limits<uint32_t>::max());
        const auto corrected = std::pow(normalized, _config.brightnessGamma);
        return static_cast<uint32_t>(
            (corrected * static_cast<float>(maxDutyValue())) + 0.5F);
    }

    [[nodiscard]] static bool isValidConfig(const Pin pin,
                                            const Config &config) {
        return isValidFrequency(config.frequency) && isValidPin(pin) &&
               isValidResolution(config.resolution) &&
               isValidBrightnessGamma(config.brightnessGamma) &&
               config.initialDuty <= maxDutyValue(config.resolution);
    }

    [[nodiscard]] static bool isValidFrequency(uint32_t frequency) {
        return frequency > 0 &&
               frequency <= 40000; // ESP32 LEDC max frequency is around 40 kHz
    }

    [[nodiscard]] static bool isValidPin(Pin pin) {
        return GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(pin));
    }

    [[nodiscard]] static bool isValidResolution(uint8_t resolution) {
        switch (resolution) {
        case LEDC_TIMER_1_BIT:
        case LEDC_TIMER_2_BIT:
        case LEDC_TIMER_3_BIT:
        case LEDC_TIMER_4_BIT:
        case LEDC_TIMER_5_BIT:
        case LEDC_TIMER_6_BIT:
        case LEDC_TIMER_7_BIT:
        case LEDC_TIMER_8_BIT:
        case LEDC_TIMER_9_BIT:
        case LEDC_TIMER_10_BIT:
        case LEDC_TIMER_11_BIT:
        case LEDC_TIMER_12_BIT:
        case LEDC_TIMER_13_BIT:
        case LEDC_TIMER_14_BIT:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] static bool isValidBrightnessGamma(float gamma) {
        return std::isfinite(gamma) && gamma > 0.0F;
    }

  private:
    Config _config;

    ledc_timer_t _timer;
    ledc_channel_t _channel;

    bool _active = false;
};

} // namespace Totem::LedPwm::detail::platform
