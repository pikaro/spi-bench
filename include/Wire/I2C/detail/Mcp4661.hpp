#pragma once

#include "Base/HasLifecycle.hpp"
#include "Wire/I2C/Interfaces/Mcp4661Config.hpp"
#include "Wire/I2C/detail/Device.hpp"
#include "Wire/I2C/detail/Master.hpp"
#include "Wire/I2C/detail/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstdint>
#include <expected>

namespace Totem::Wire::I2C::detail {

class Mcp4661 : public HasLifecycle<Mcp4661, Mcp4661Config> {
    friend class HasLifecycle<Mcp4661, Mcp4661Config>;
    friend struct LifecycleContract<Mcp4661, Mcp4661Config>;

  public:
    DELETE_COPY(Mcp4661)
    DELETE_MOVE(Mcp4661)

    static constexpr const char *name = "I2C::Mcp4661";
    static constexpr LogComponent logComponent =
        Totem::Wire::I2C::detail::logComponent;

    explicit Mcp4661(Master &master) : _master(master) {}

    ReturnCode writeVolatile(Mcp4661Wiper wiper, uint16_t value) {
        FAIL_IF(!_validWiper(wiper), ERR(CoreError, InvalidArgument),
                "Invalid MCP4661 wiper");
        FAIL_IF(value > mcp4661MaxWiperValue, ERR(CoreError, InvalidArgument),
                "MCP4661 wiper value %u exceeds max %u", value,
                mcp4661MaxWiperValue);

        FAIL_IF_ERR_FWD(_writeValue(_volatileRegister(wiper), value),
                        "Failed to write MCP4661 volatile wiper");
        _wiperCache[static_cast<size_t>(wiper)] = value;
        return OK();
    }

    ReturnCode writeEeprom(Mcp4661Wiper wiper, uint16_t value) {
        FAIL_IF(!_validWiper(wiper), ERR(CoreError, InvalidArgument),
                "Invalid MCP4661 wiper");
        FAIL_IF(value > mcp4661MaxWiperValue, ERR(CoreError, InvalidArgument),
                "MCP4661 wiper value %u exceeds max %u", value,
                mcp4661MaxWiperValue);
        return _writeValue(_eepromRegister(wiper), value);
    }

    ReturnCode writeVolatileAndEeprom(Mcp4661Wiper wiper, uint16_t value) {
        FAIL_IF_ERR_FWD(writeVolatile(wiper, value),
                        "Failed to write MCP4661 volatile value");
        return writeEeprom(wiper, value);
    }

    std::expected<uint16_t, ReturnCode> cachedWiper(Mcp4661Wiper wiper) const {
        FAIL_IF(!_validWiper(wiper),
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "Invalid MCP4661 wiper");
        return _wiperCache[static_cast<size_t>(wiper)];
    }

    std::expected<uint16_t, ReturnCode> readVolatile(Mcp4661Wiper wiper) {
        FAIL_IF(!_validWiper(wiper),
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "Invalid MCP4661 wiper");
        auto value = _readValue(_volatileRegister(wiper));
        if (!value) {
            return value;
        }
        const auto masked =
            static_cast<uint16_t>(*value & mcp4661MaxWiperValue);
        _wiperCache[static_cast<size_t>(wiper)] = masked;
        return masked;
    }

    ReturnCode increment(Mcp4661Wiper wiper) {
        FAIL_IF(!_validWiper(wiper), ERR(CoreError, InvalidArgument),
                "Invalid MCP4661 wiper");
        auto &cached = _wiperCache[static_cast<size_t>(wiper)];
        FAIL_IF(cached >= mcp4661MaxWiperValue, ERR(CoreError, Overflow),
                "MCP4661 wiper already at maximum");
        FAIL_IF_ERR_FWD(_sendCommand(_volatileRegister(wiper),
                                     Command::Increment),
                        "Failed to increment MCP4661 wiper");
        ++cached;
        return OK();
    }

    ReturnCode decrement(Mcp4661Wiper wiper) {
        FAIL_IF(!_validWiper(wiper), ERR(CoreError, InvalidArgument),
                "Invalid MCP4661 wiper");
        auto &cached = _wiperCache[static_cast<size_t>(wiper)];
        FAIL_IF(cached == 0, ERR(CoreError, Underflow),
                "MCP4661 wiper already at minimum");
        FAIL_IF_ERR_FWD(_sendCommand(_volatileRegister(wiper),
                                     Command::Decrement),
                        "Failed to decrement MCP4661 wiper");
        --cached;
        return OK();
    }

    std::expected<uint8_t, ReturnCode> readStatus() {
        auto value = _readValue(Mcp4661Register::Status);
        if (!value) {
            return std::unexpected(value.error());
        }
        return static_cast<uint8_t>(*value & 0x00FFU);
    }

    std::expected<uint16_t, ReturnCode> readTcon() {
        return _readValue(Mcp4661Register::Tcon);
    }

    ReturnCode writeTcon(uint16_t value) {
        return _writeValue(Mcp4661Register::Tcon, value);
    }

    ReturnCode setTcon(Mcp4661TconFlag flag, bool enabled) {
        auto current = readTcon();
        if (!current) {
            return current.error();
        }
        const auto mask =
            static_cast<uint16_t>(1U << static_cast<uint8_t>(flag));
        auto next = *current;
        if (enabled) {
            next = static_cast<uint16_t>(next | mask);
        } else {
            next = static_cast<uint16_t>(next & ~mask);
        }
        return writeTcon(next);
    }

  private:
    enum class Command : uint8_t {
        Write = 0b00,
        Increment = 0b01,
        Decrement = 0b10,
        Read = 0b11,
    };

    ReturnCode _onBegin() {
        FAIL_IF(!_master.active(), ERR(CoreError, InvalidState),
                "Cannot begin MCP4661 before I2C master is active");
        FAIL_IF_ERR_FWD(_device.begin(_master, config().device),
                        "Failed to register MCP4661 I2C device");
        _wiperCache = {config().initialWiper0, config().initialWiper1};
        _lastCommandMs = 0;

        if (config().writeInitialState) {
            auto ret = _writeInitialState();
            if (!ret.ok()) {
                (void)_device.end();
                return ret;
            }
        }
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        if (config().writeInitialStateOnEnd && _device.active()) {
            ret.combine(_writeInitialState());
        }
        ret.combine(_device.end());
        _wiperCache = {};
        _lastCommandMs = 0;
        return ret;
    }

    ReturnCode _writeInitialState() {
        auto ret = writeVolatile(Mcp4661Wiper::Wiper0, config().initialWiper0);
        ret.combine(
            writeVolatile(Mcp4661Wiper::Wiper1, config().initialWiper1));
        return ret;
    }

    ReturnCode _writeValue(Mcp4661Register reg, uint16_t value) {
        FAIL_IF(value > mcp4661MaxWiperValue, ERR(CoreError, InvalidArgument),
                "MCP4661 value %u exceeds max %u", value,
                mcp4661MaxWiperValue);
        const std::array<uint8_t, 2> payload{{
            _commandByte(reg, Command::Write, value),
            static_cast<uint8_t>(value & 0x00FFU),
        }};
        return _writeRaw(payload);
    }

    std::expected<uint16_t, ReturnCode> _readValue(Mcp4661Register reg) {
        FAIL_IF(!_device.active(),
                std::unexpected(ERR(CoreError, InvalidState)),
                "MCP4661 device is not active");
        const std::array<uint8_t, 1> command{{
            _commandByte(reg, Command::Read, 0),
        }};
        std::array<uint8_t, 2> payload{};
        _waitCommandSpacing();
        auto ret = _device.writeRead(command, payload);
        _recordCommandTime();
        if (!ret.ok()) {
            return std::unexpected(ret);
        }
        return static_cast<uint16_t>((static_cast<uint16_t>(payload[0]) << 8U) |
                                     payload[1]);
    }

    ReturnCode _sendCommand(Mcp4661Register reg, Command command) {
        const std::array<uint8_t, 1> payload{{
            _commandByte(reg, command, 0),
        }};
        return _writeRaw(payload);
    }

    template <size_t Size>
    ReturnCode _writeRaw(const std::array<uint8_t, Size> &payload) {
        FAIL_IF(!_device.active(), ERR(CoreError, InvalidState),
                "MCP4661 device is not active");
        _waitCommandSpacing();
        auto ret = _device.write(payload);
        _recordCommandTime();
        return ret;
    }

    void _waitCommandSpacing() const {
        if (config().commandSpacingMs == 0 || _lastCommandMs == 0) {
            return;
        }
        const auto nowMs = ::platform::get_time();
        const auto elapsed = static_cast<uint32_t>(nowMs - _lastCommandMs);
        if (elapsed < config().commandSpacingMs) {
            ::platform::delay(
                ::platform::ms_to_ticks(config().commandSpacingMs - elapsed));
        }
    }

    void _recordCommandTime() { _lastCommandMs = ::platform::get_time(); }

    static constexpr bool _validWiper(Mcp4661Wiper wiper) {
        return static_cast<uint8_t>(wiper) < 2U;
    }

    static constexpr Mcp4661Register _volatileRegister(Mcp4661Wiper wiper) {
        return wiper == Mcp4661Wiper::Wiper0
                   ? Mcp4661Register::Wiper0Volatile
                   : Mcp4661Register::Wiper1Volatile;
    }

    static constexpr Mcp4661Register _eepromRegister(Mcp4661Wiper wiper) {
        return wiper == Mcp4661Wiper::Wiper0 ? Mcp4661Register::Wiper0Eeprom
                                             : Mcp4661Register::Wiper1Eeprom;
    }

    static constexpr uint8_t _commandByte(Mcp4661Register reg, Command command,
                                          uint16_t data) {
        return static_cast<uint8_t>(
            ((static_cast<uint8_t>(reg) & 0x0FU) << 4U) |
            ((static_cast<uint8_t>(command) & 0x03U) << 2U) |
            ((data >> 8U) & 0x03U));
    }

    Master &_master;
    Device _device{};
    std::array<uint16_t, 2> _wiperCache{};
    uint32_t _lastCommandMs = 0;
};

} // namespace Totem::Wire::I2C::detail
