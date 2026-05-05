#pragma once

#include "Base/HasLifecycle.hpp"
#include "Wire/I2C/Interfaces/Pcf8574Config.hpp"
#include "Wire/I2C/detail/Device.hpp"
#include "Wire/I2C/detail/Master.hpp"
#include "Wire/I2C/detail/Types.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstdint>
#include <expected>

namespace Totem::Wire::I2C::detail {

class Pcf8574 : public HasLifecycle<Pcf8574, Pcf8574Config> {
    friend class HasLifecycle<Pcf8574, Pcf8574Config>;
    friend struct LifecycleContract<Pcf8574, Pcf8574Config>;

  public:
    DELETE_COPY(Pcf8574)
    DELETE_MOVE(Pcf8574)

    static constexpr const char *name = "I2C::Pcf8574";
    static constexpr LogComponent logComponent =
        Totem::Wire::I2C::detail::logComponent;

    explicit Pcf8574(Master &master) : _master(master) {}

    [[nodiscard]] uint8_t releasedMask() const { return _releasedMask; }
    [[nodiscard]] bool pinReleased(Pcf8574Pin pin) const {
        return (_releasedMask & _pinMask(pin)) != 0;
    }

    ReturnCode writePort(uint8_t releasedMask) {
        FAIL_IF_INACTIVE_ERR("Cannot write inactive PCF8574");
        return _writePortRaw(releasedMask);
    }

    ReturnCode writePin(Pcf8574Pin pin, Pcf8574PinState state) {
        FAIL_IF(!_validPin(pin), ERR(CoreError, InvalidArgument),
                "Invalid PCF8574 pin");
        auto next = _releasedMask;
        if (state == Pcf8574PinState::Released) {
            next |= _pinMask(pin);
        } else {
            next &= static_cast<uint8_t>(~_pinMask(pin));
        }
        return writePort(next);
    }

    std::expected<uint8_t, ReturnCode> readPort() {
        FAIL_IF_INACTIVE_UNEXPECTED("Cannot read inactive PCF8574");
        return _readPortRaw();
    }

    std::expected<bool, ReturnCode> readPinLevel(Pcf8574Pin pin) {
        FAIL_IF(!_validPin(pin),
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "Invalid PCF8574 pin");
        auto port = readPort();
        if (!port) {
            return std::unexpected(port.error());
        }
        return ((*port & _pinMask(pin)) != 0);
    }

  private:
    ReturnCode _onBegin() {
        FAIL_IF(!_master.active(), ERR(CoreError, InvalidState),
                "Cannot begin PCF8574 before I2C master is active");
        FAIL_IF_ERR_FWD(_device.begin(_master, config().device),
                        "Failed to register PCF8574 I2C device");
        _releasedMask = 0xFF;
        if (config().writeInitialState) {
            auto ret = _writePortRaw(config().initialReleasedMask);
            if (!ret.ok()) {
                (void)_device.end();
                return ret;
            }
        }
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        if (config().writeShutdownState && _device.active()) {
            ret.combine(_writePortRaw(config().shutdownReleasedMask));
        }
        ret.combine(_device.end());
        _releasedMask = 0xFF;
        return ret;
    }

    ReturnCode _writePortRaw(uint8_t releasedMask) {
        const std::array<uint8_t, 1> payload{{releasedMask}};
        FAIL_IF_ERR_FWD(_device.write(payload), "Failed to write PCF8574 port");
        _releasedMask = releasedMask;
        return OK();
    }

    std::expected<uint8_t, ReturnCode> _readPortRaw() {
        std::array<uint8_t, 1> payload{};
        auto ret = _device.read(payload);
        if (!ret.ok()) {
            return std::unexpected(ret);
        }
        return payload[0];
    }

    static constexpr bool _validPin(Pcf8574Pin pin) {
        return static_cast<uint8_t>(pin) < 8U;
    }

    static constexpr uint8_t _pinMask(Pcf8574Pin pin) {
        return static_cast<uint8_t>(1U << static_cast<uint8_t>(pin));
    }

    Master &_master;
    Device _device{};
    uint8_t _releasedMask = 0xFF;
};

} // namespace Totem::Wire::I2C::detail
