#pragma once

#include "LedPwm/Interfaces/Config.hpp"
#include "LedPwm/Interfaces/Types.hpp"
#include "LedPwm/detail/Animator.hpp"
#include "LedPwm/detail/PlatformSelect.hpp"
#include "LedPwm/detail/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "StaticConfig/LedPwm.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace Totem::LedPwm::detail {

class Handler {
    struct PlatformSlot {
        bool used = false;
        Platform platform;
        PeripheralLedConfig config;
    };

  public:
    ReturnCode init(const Config &config, LedHandle handle) {
        FAIL_IF(handle.idx >= _platforms.size(), ERR(InvalidArgument),
                "Invalid LED handle index %u", handle.idx);
        const auto &ledCfg = config.leds[handle.idx];
        FAIL_IF(_platforms[handle.idx].used, ERR(LifecycleError, Active),
                "LED " SV_FMT " on pin %u is already initialized",
                MAGIC_SV_ARG(ledCfg.led), ledCfg.pin);
        FAIL_IF_ERR_FWD(_initPlatform(config, handle),
                        "Failed to initialize LED " SV_FMT " on pin %u",
                        MAGIC_SV_ARG(ledCfg.led), ledCfg.pin);
        _log_i("Initialized LED " SV_FMT " on pin %u", MAGIC_SV_ARG(ledCfg.led),
               ledCfg.pin);
        return OK();
    }

    ReturnCode deinit(LedHandle handle) {
        FAIL_IF_ERR_FWD(_validateHandle(handle),
                        "Cannot deinitialize LED handle %u", handle.idx);
        auto &slot = _platforms[handle.idx];
        FAIL_IF_ERR_FWD(slot.platform.deinit(),
                        "Failed to deinitialize LED " SV_FMT " on pin %u",
                        MAGIC_SV_ARG(slot.config.led), slot.config.pin);
        slot.used = false;
        _log_i("Deinitialized LED " SV_FMT " on pin %u",
               MAGIC_SV_ARG(slot.config.led), slot.config.pin);
        return OK();
    }

    ReturnCode handle(LedHandle led, const LedCommand &cmd) {
        return handle(led, cmd, ::platform::get_time());
    }

    ReturnCode handle(LedHandle led, const LedCommand &cmd, uint32_t nowMs) {
        return std::visit(
            [this, led, nowMs](const auto &payload) -> ReturnCode {
                return handlePayload(led, payload, nowMs);
            },
            cmd.payload);
    }

    ReturnCode step(uint32_t nowMs) {
        auto ret = OK();
        // Active animation slot gauges are intentionally omitted here. This
        // task runs at animation cadence, and per-LED slot metrics would add
        // metric traffic to every visual frame for data that is only useful
        // while profiling a specific effect.
        for (size_t i = 0; i < _platforms.size(); ++i) {
            if (!_platforms[i].used) {
                continue;
            }
            const auto brightness = _animators[i].step(nowMs);
            if (!brightness.has_value()) {
                continue;
            }
            auto handle = LedHandle{.idx = static_cast<uint8_t>(i)};
            ret.combine(applyBrightness(handle, *brightness));
        }
        return ret;
    }

    ReturnCode setDuty(LedHandle handle, Duty duty) {
        FAIL_IF_ERR_FWD(_validateHandle(handle),
                        "Cannot set duty for LED handle %u", handle.idx);
        _animators[handle.idx].reset();
        auto &slot = _platforms[handle.idx];
        FAIL_IF_ERR_FWD(slot.platform.setDutyScaled(duty.scaledToUint32()),
                        "Failed to set duty for LED " SV_FMT " on pin %u",
                        MAGIC_SV_ARG(slot.config.led), slot.config.pin);
        return OK();
    }

    ReturnCode setBrightness(LedHandle handle, Brightness brightness) {
        FAIL_IF_ERR_FWD(_validateHandle(handle),
                        "Cannot set brightness for LED handle %u", handle.idx);
        FAIL_IF_ERR_FWD(_animators[handle.idx].setBase(brightness),
                        "Cannot set base brightness for LED handle %u",
                        handle.idx);
        auto &slot = _platforms[handle.idx];
        FAIL_IF_ERR_FWD(applyBrightness(handle, brightness),
                        "Failed to set brightness for LED " SV_FMT " on pin %u",
                        MAGIC_SV_ARG(slot.config.led), slot.config.pin);
        return OK();
    }

  private:
    ReturnCode handlePayload(LedHandle handle, const SetDuty &cmd,
                             uint32_t /*unused*/) {
        FAIL_IF_NOT(cmd.validate(), ERR(InvalidArgument),
                    "Invalid set-duty LED command");
        return setDuty(handle, cmd.duty);
    }

    ReturnCode handlePayload(LedHandle handle, const SetBrightness &cmd,
                             uint32_t /*unused*/) {
        FAIL_IF_NOT(cmd.validate(), ERR(InvalidArgument),
                    "Invalid set-brightness LED command");
        return setBrightness(handle, cmd.brightness);
    }

    ReturnCode handlePayload(LedHandle handle, const StartAnimation &cmd,
                             uint32_t nowMs) {
        FAIL_IF_ERR_FWD(_validateHandle(handle),
                        "Cannot start animation for LED handle %u", handle.idx);
        FAIL_IF_NOT(cmd.validate(), ERR(InvalidArgument),
                    "Invalid start-animation LED command");
        return _animators[handle.idx].start(cmd.animation, nowMs);
    }

    ReturnCode handlePayload(LedHandle handle, const ClearAnimations &cmd,
                             uint32_t /*unused*/) {
        FAIL_IF_ERR_FWD(_validateHandle(handle),
                        "Cannot clear animations for LED handle %u",
                        handle.idx);
        FAIL_IF_NOT(cmd.validate(), ERR(InvalidArgument),
                    "Invalid clear-animations LED command");
        return _animators[handle.idx].clearAnimations();
    }

    ReturnCode applyBrightness(LedHandle handle, Brightness brightness) {
        FAIL_IF_ERR_FWD(_validateHandle(handle),
                        "Cannot apply brightness for LED handle %u",
                        handle.idx);
        auto &slot = _platforms[handle.idx];
        FAIL_IF_ERR_FWD(
            slot.platform.setBrightnessScaled(brightness.scaledToUint32()),
            "Failed to apply brightness for LED " SV_FMT " on pin %u",
            MAGIC_SV_ARG(slot.config.led), slot.config.pin);
        return OK();
    }

    ReturnCode _validateHandle(LedHandle handle) const {
        FAIL_IF(handle.idx >= _platforms.size(), ERR(InvalidArgument),
                "Invalid LED handle index %u", handle.idx);
        const auto &slot = _platforms[handle.idx];
        FAIL_IF_NOT(slot.used, ERR(LifecycleError, NotActive),
                    "LED " SV_FMT " on pin %u is not initialized",
                    MAGIC_SV_ARG(slot.config.led), slot.config.pin);
        return OK();
    }

    ReturnCode _initPlatform(const Config &config, LedHandle handle) {
        const auto &ledCfg = config.leds[handle.idx];
        auto &slot = _platforms[handle.idx];

        slot.used = true;
        slot.platform = Platform{};
        slot.config = ledCfg;

        auto initResult = slot.platform.init(ledCfg.pin, config.platform);
        if (!initResult.ok()) {
            slot.used = false;
            slot.platform = Platform{};
            FAIL_ERR_FWD(initResult,
                         "Failed to initialize LED " SV_FMT " on pin %u",
                         MAGIC_SV_ARG(ledCfg.led), ledCfg.pin);
        }
        return OK();
    }

    std::array<PlatformSlot, LedPwmConfig::maxLeds> _platforms;
    std::array<Animator, LedPwmConfig::maxLeds> _animators;
};

} // namespace Totem::LedPwm::detail
