#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasMutex.hpp"
#include "Wire/I2C/Interfaces/MasterConfig.hpp"
#include "Wire/I2C/Interfaces/Types.hpp"
#include "Wire/I2C/detail/Metrics.hpp"
#include "Wire/I2C/detail/PlatformSelect.hpp"
#include "Wire/I2C/detail/Types.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <cstdint>
#include <expected>
#include <span>

namespace Totem::Wire::I2C::detail {

class Master : public HasLifecycle<Master, MasterConfig>,
               public HasMutex<Master> {
    friend class HasLifecycle<Master, MasterConfig>;
    friend struct LifecycleContract<Master, MasterConfig>;
    friend struct MutexContract<Master>;

  public:
    DELETE_COPY(Master)
    DELETE_MOVE(Master)

    static constexpr const char *name = "I2C::Master";
    static constexpr LogComponent logComponent =
        Totem::Wire::I2C::detail::logComponent;

    Master() = default;

    // Registers a target address on this bus. The returned handle is cheap to
    // store by value and remains valid until unregistered or the bus ends.
    std::expected<DeviceHandle, ReturnCode>
    registerDevice(const DeviceConfig &config) {
        FAIL_IF_INACTIVE_UNEXPECTED(
            "Cannot register I2C device before master is active");
        auto guard = _mutexGuard();
        if (!guard.acquired()) {
            metrics().addLockTimeout();
            FAIL(std::unexpected(ERR(CoreError, Timeout)),
                 "Failed to take I2C master mutex");
        }
        auto result = _bus.addDevice(config);
        if (!result) {
            metrics().addResult(result.error(), 0);
            return result;
        }
        metrics().addDevice();
        return result;
    }

    ReturnCode unregisterDevice(DeviceHandle device) {
        if (!device.valid()) {
            return OK();
        }
        FAIL_IF_INACTIVE_ERR(
            "Cannot unregister I2C device before master is active");
        auto guard = _mutexGuard();
        if (!guard.acquired()) {
            metrics().addLockTimeout();
            FAIL(ERR(CoreError, Timeout), "Failed to take I2C master mutex");
        }
        auto ret = _bus.removeDevice(device);
        metrics().addResult(ret, 0);
        if (ret.ok()) {
            metrics().removeDevice();
        }
        return ret;
    }

    ReturnCode write(DeviceHandle device, std::span<const uint8_t> data,
                     uint32_t timeoutMs = 0) {
        FAIL_IF_INACTIVE_ERR("Cannot write before I2C master is active");
        auto guard = _mutexGuard(timeoutMs == 0 ? config().transactionTimeoutMs
                                                : timeoutMs);
        if (!guard.acquired()) {
            metrics().addLockTimeout();
            FAIL(ERR(CoreError, Timeout), "Failed to take I2C master mutex");
        }
        const auto startedUs = _profilingTimestampUs();
        auto ret = _bus.write(device, data, _timeout(timeoutMs));
        metrics().addWrite(data.size(), ret, _profilingElapsedUs(startedUs));
        return ret;
    }

    ReturnCode read(DeviceHandle device, std::span<uint8_t> data,
                    uint32_t timeoutMs = 0) {
        FAIL_IF_INACTIVE_ERR("Cannot read before I2C master is active");
        auto guard = _mutexGuard(timeoutMs == 0 ? config().transactionTimeoutMs
                                                : timeoutMs);
        if (!guard.acquired()) {
            metrics().addLockTimeout();
            FAIL(ERR(CoreError, Timeout), "Failed to take I2C master mutex");
        }
        const auto startedUs = _profilingTimestampUs();
        auto ret = _bus.read(device, data, _timeout(timeoutMs));
        metrics().addRead(data.size(), ret, _profilingElapsedUs(startedUs));
        return ret;
    }

    ReturnCode writeRead(DeviceHandle device, std::span<const uint8_t> command,
                         std::span<uint8_t> data, uint32_t timeoutMs = 0) {
        FAIL_IF_INACTIVE_ERR("Cannot write-read before I2C master is active");
        auto guard = _mutexGuard(timeoutMs == 0 ? config().transactionTimeoutMs
                                                : timeoutMs);
        if (!guard.acquired()) {
            metrics().addLockTimeout();
            FAIL(ERR(CoreError, Timeout), "Failed to take I2C master mutex");
        }
        const auto startedUs = _profilingTimestampUs();
        auto ret = _bus.writeRead(device, command, data, _timeout(timeoutMs));
        metrics().addWriteRead(command.size(), data.size(), ret,
                               _profilingElapsedUs(startedUs));
        return ret;
    }

  private:
    ReturnCode _onBegin() {
        prewarmMetrics();
        _defaultMutexTimeoutMs = config().transactionTimeoutMs;
        const auto &pins = config().pins;
        _log_i("Starting I2C master: bus=%u, clock=%lu Hz, sda=%d, scl=%d, "
               "deviceSlots=%u",
               static_cast<unsigned>(config().busId),
               static_cast<unsigned long>(config().clockHz),
               static_cast<int>(pins.sda), static_cast<int>(pins.scl),
               static_cast<unsigned>(maxDevicesPerMaster));
        return _bus.init(config());
    }

    ReturnCode _onEnd() {
        auto guard = _mutexGuard();
        if (!guard.acquired()) {
            metrics().addLockTimeout();
            FAIL(ERR(CoreError, Timeout), "Failed to take I2C master mutex");
        }
        auto ret = _bus.deinit();
        metrics().addResult(ret, 0);
        if (ret.ok()) {
            metrics().clearDevices();
        }
        return ret;
    }

    [[nodiscard]] uint32_t _timeout(uint32_t overrideMs) const {
        return overrideMs == 0 ? config().transactionTimeoutMs : overrideMs;
    }

    static int64_t _profilingTimestampUs() {
        if constexpr (Metrics::profilingEnabled) {
            return ::platform::get_time_us();
        }
        return 0;
    }

    static uint32_t _profilingElapsedUs(int64_t startedUs) {
        if constexpr (Metrics::profilingEnabled) {
            const auto nowUs = ::platform::get_time_us();
            if (nowUs >= startedUs) {
                return static_cast<uint32_t>(nowUs - startedUs);
            }
        }
        return 0;
    }

    Platform::MasterBus _bus{};
};

} // namespace Totem::Wire::I2C::detail
