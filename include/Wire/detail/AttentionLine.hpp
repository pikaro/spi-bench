#pragma once

#include "Macros/Facade.hpp"
#include "Platform/Gpio.hpp"
#include "Platform/Hardware.hpp"
#include "Types/Error.hpp"
#include "Types/Gpio.hpp"
#include <cstdint>
#include <expected>
#include <optional>

namespace Totem::Wire::detail {

enum class AttentionLineEvent : uint8_t {
    Asserted,
    Released,
};

using AttentionLineCallback = void (*)(void *owner, AttentionLineEvent event);

class AttentionLine {
  public:
    DELETE_COPY(AttentionLine)
    DELETE_MOVE(AttentionLine)

    AttentionLine() = default;

    ReturnCode initInput(std::optional<Pin> pin, void *owner,
                         AttentionLineCallback callback) {
        if (!pin.has_value()) {
            return OK();
        }
        FAIL_IF(owner == nullptr || callback == nullptr,
                ERR(CoreError, InvalidArgument),
                "Invalid wire attention input callback");

        _pin = pin;
        _owner = owner;
        _callback = callback;
        _input = true;

        FAIL_IF_ERR_FWD(
            _gpio.initInput(*pin, GpioPull::Up, GpioInterrupt::AnyEdge),
            "Failed to configure wire attention input");
        FAIL_IF_ERR_FWD(_gpio.registerIsr(this, _onGpioInterrupt),
                        "Failed to register wire attention input ISR");
        return OK();
    }

    ReturnCode initOutput(std::optional<Pin> pin) {
        if (!pin.has_value()) {
            return OK();
        }
        _pin = pin;
        _input = false;
        return _gpio.initOutput(*pin, GpioOutputMode::OpenDrain, true);
    }

    ReturnCode setAsserted(bool asserted) {
        if (!_pin.has_value()) {
            return OK();
        }
        FAIL_IF(_input, ERR(CoreError, InvalidState),
                "Cannot drive wire attention input");
        return _gpio.setLevel(!asserted);
    }

    std::expected<bool, ReturnCode> asserted() const {
        if (!_pin.has_value()) {
            return false;
        }
        auto level = _gpio.level();
        if (!level) {
            return std::unexpected(level.error());
        }
        return !*level;
    }

    ReturnCode deinit() {
        if (!_pin.has_value()) {
            return OK();
        }
        if (!_input) {
            (void)setAsserted(false);
        }
        _owner = nullptr;
        _callback = nullptr;
        _input = false;
        _pin.reset();
        return _gpio.deinit();
    }

    [[nodiscard]] bool configured() const { return _pin.has_value(); }

  private:
    static void _onGpioInterrupt(void *owner, GpioEvent event) {
        auto *self = static_cast<AttentionLine *>(owner);
        if (self == nullptr || self->_callback == nullptr) {
            return;
        }
        self->_callback(self->_owner, event.level
                                          ? AttentionLineEvent::Released
                                          : AttentionLineEvent::Asserted);
    }

    ::platform::Gpio _gpio;
    std::optional<Pin> _pin = std::nullopt;
    void *_owner = nullptr;
    AttentionLineCallback _callback = nullptr;
    bool _input = false;
};

} // namespace Totem::Wire::detail
