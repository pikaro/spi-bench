#pragma once

#include "Wire/I2C/Interfaces/MasterConfig.hpp"
#include "Wire/I2C/Interfaces/Types.hpp"
#include "Wire/I2C/detail/Master.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <cstdint>
#include <span>

namespace Totem::Wire::I2C::detail {

class Device {
  public:
    DELETE_COPY(Device)
    DELETE_MOVE(Device)

    Device() = default;

    // Registers one address on the shared master bus. Concrete device drivers
    // compose this handle instead of owning platform driver state directly.
    ReturnCode begin(Master &master, const DeviceConfig &config) {
        FAIL_IF(active(), ERR(CoreError, InvalidState),
                "I2C device is already active");
        FAIL_IF(!config.validate(), ERR(CoreError, InvalidArgument),
                "Invalid I2C device config");

        auto handleResult = master.registerDevice(config);
        if (!handleResult) {
            return handleResult.error();
        }

        _master = &master;
        _handle = *handleResult;
        _config = config;
        return OK();
    }

    ReturnCode end() {
        if (!active()) {
            return OK();
        }
        auto ret = _master->unregisterDevice(_handle);
        _master = nullptr;
        _handle = {};
        _config = {};
        return ret;
    }

    [[nodiscard]] bool active() const {
        return _master != nullptr && _handle.valid();
    }
    [[nodiscard]] DeviceHandle handle() const { return _handle; }
    [[nodiscard]] const DeviceConfig &config() const { return _config; }

    ReturnCode write(std::span<const uint8_t> data, uint32_t timeoutMs = 0) {
        FAIL_IF(!active(), ERR(CoreError, InvalidState),
                "I2C device is not active");
        return _master->write(_handle, data, timeoutMs);
    }

    ReturnCode read(std::span<uint8_t> data, uint32_t timeoutMs = 0) {
        FAIL_IF(!active(), ERR(CoreError, InvalidState),
                "I2C device is not active");
        return _master->read(_handle, data, timeoutMs);
    }

    ReturnCode writeRead(std::span<const uint8_t> command,
                         std::span<uint8_t> data, uint32_t timeoutMs = 0) {
        FAIL_IF(!active(), ERR(CoreError, InvalidState),
                "I2C device is not active");
        return _master->writeRead(_handle, command, data, timeoutMs);
    }

  private:
    Master *_master = nullptr;
    DeviceHandle _handle{};
    DeviceConfig _config{};
};

} // namespace Totem::Wire::I2C::detail
