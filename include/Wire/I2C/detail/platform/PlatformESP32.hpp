// IWYU pragma: private

#pragma once

#include "Wire/I2C/Interfaces/MasterConfig.hpp"
#include "Wire/I2C/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "soc/soc_caps.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace Totem::Wire::I2C::detail::platform {

inline gpio_num_t pinValue(Pin pin) {
    return static_cast<gpio_num_t>(static_cast<uint8_t>(pin));
}

inline std::expected<i2c_port_num_t, ReturnCode> portFor(BusId busId) {
    switch (busId) {
    case BusId::Bus0:
        return I2C_NUM_0;
    case BusId::Bus1:
#if SOC_I2C_NUM > 1
        return I2C_NUM_1;
#else
        return std::unexpected(ERR(CoreError, InvalidArgument));
#endif
    default:
        return std::unexpected(ERR(CoreError, InvalidArgument));
    }
}

inline i2c_addr_bit_len_t addressBitsFor(AddressBits addressBits) {
    switch (addressBits) {
    case AddressBits::Ten:
        return I2C_ADDR_BIT_LEN_10;
    case AddressBits::Seven:
    default:
        return I2C_ADDR_BIT_LEN_7;
    }
}

class MasterBus {
  public:
    DELETE_COPY(MasterBus)
    DELETE_MOVE(MasterBus)

    MasterBus() = default;

    ReturnCode init(const MasterConfig &config) {
        FAIL_IF(_initialized, ERR(CoreError, InvalidState),
                "I2C master bus already initialized");
        auto portResult = portFor(config.busId);
        if (!portResult) {
            return portResult.error();
        }

        i2c_master_bus_config_t busConfig{};
        busConfig.i2c_port = *portResult;
        busConfig.sda_io_num = pinValue(config.pins.sda);
        busConfig.scl_io_num = pinValue(config.pins.scl);
        busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
        busConfig.glitch_ignore_cnt = config.glitchIgnoreCount;
        busConfig.intr_priority = 0;
        busConfig.trans_queue_depth = 0;
        busConfig.flags.enable_internal_pullup =
            config.enableInternalPullups ? 1U : 0U;

        FAIL_IF_PLATFORM_FWD(i2c_new_master_bus(&busConfig, &_bus),
                             "Failed to initialize I2C master bus");
        _clockHz = config.clockHz;
        _initialized = true;
        return OK();
    }

    std::expected<DeviceHandle, ReturnCode>
    addDevice(const DeviceConfig &config) {
        FAIL_IF(!_initialized, std::unexpected(ERR(CoreError, InvalidState)),
                "Cannot add I2C device before bus init");
        FAIL_IF(!config.validate(),
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "Invalid I2C device config");
        FAIL_IF(_findDevice(config.address).has_value(),
                std::unexpected(ERR(CoreError, AlreadyExists)),
                "I2C device address 0x%02X is already registered",
                config.address);

        i2c_device_config_t deviceConfig{};
        deviceConfig.dev_addr_length = addressBitsFor(config.addressBits);
        deviceConfig.device_address = config.address;
        deviceConfig.scl_speed_hz =
            config.clockHz == 0 ? _clockHz : config.clockHz;
        deviceConfig.scl_wait_us = config.sclWaitUs;
        deviceConfig.flags.disable_ack_check =
            config.disableAckCheck ? 1U : 0U;

        i2c_master_dev_handle_t handle = nullptr;
        FAIL_IF_PLATFORM_FWD_UNEXPECTED(
            i2c_master_bus_add_device(_bus, &deviceConfig, &handle),
            "Failed to add I2C device at 0x%02X", config.address);

        auto stored = _storeDevice(config.address, handle);
        if (!stored) {
            (void)i2c_master_bus_rm_device(handle);
            return std::unexpected(stored.error());
        }
        return *stored;
    }

    ReturnCode removeDevice(DeviceHandle device) {
        FAIL_IF(!_initialized, ERR(CoreError, InvalidState),
                "Cannot remove I2C device before bus init");
        auto entry = _entryFor(device);
        if (!entry) {
            return entry.error();
        }

        FAIL_IF_PLATFORM_FWD(i2c_master_bus_rm_device((*entry)->handle),
                             "Failed to remove I2C device");
        _devices[device.index] = {};
        return OK();
    }

    ReturnCode write(DeviceHandle device, std::span<const uint8_t> data,
                     uint32_t timeoutMs) const {
        FAIL_IF(!data.empty() && data.data() == nullptr,
                ERR(CoreError, InvalidArgument), "Invalid I2C write buffer");
        FAIL_IF(data.empty(), ERR(CoreError, InvalidSize),
                "Cannot issue empty I2C write");
        auto entry = _entryFor(device);
        if (!entry) {
            return entry.error();
        }
        return ::platform::map_platform_error(i2c_master_transmit(
            (*entry)->handle, data.data(), data.size(),
            static_cast<int>(timeoutMs)));
    }

    ReturnCode read(DeviceHandle device, std::span<uint8_t> data,
                    uint32_t timeoutMs) const {
        FAIL_IF(!data.empty() && data.data() == nullptr,
                ERR(CoreError, InvalidArgument), "Invalid I2C read buffer");
        FAIL_IF(data.empty(), ERR(CoreError, InvalidSize),
                "Cannot issue empty I2C read");
        auto entry = _entryFor(device);
        if (!entry) {
            return entry.error();
        }
        return ::platform::map_platform_error(i2c_master_receive(
            (*entry)->handle, data.data(), data.size(),
            static_cast<int>(timeoutMs)));
    }

    ReturnCode writeRead(DeviceHandle device, std::span<const uint8_t> command,
                         std::span<uint8_t> data,
                         uint32_t timeoutMs) const {
        FAIL_IF(command.empty() || data.empty(), ERR(CoreError, InvalidSize),
                "I2C write-read requires command and read buffers");
        FAIL_IF(command.data() == nullptr || data.data() == nullptr,
                ERR(CoreError, InvalidArgument),
                "Invalid I2C write-read buffer");
        auto entry = _entryFor(device);
        if (!entry) {
            return entry.error();
        }
        return ::platform::map_platform_error(i2c_master_transmit_receive(
            (*entry)->handle, command.data(), command.size(), data.data(),
            data.size(), static_cast<int>(timeoutMs)));
    }

    ReturnCode deinit() {
        if (!_initialized) {
            return OK();
        }

        ReturnCode ret = OK();
        for (auto &device : _devices) {
            if (device.handle != nullptr) {
                ret.combine(
                    ::platform::map_platform_error(i2c_master_bus_rm_device(
                        device.handle)));
                device = {};
            }
        }
        ret.combine(::platform::map_platform_error(i2c_del_master_bus(_bus)));
        _bus = nullptr;
        _clockHz = 0;
        _initialized = false;
        return ret;
    }

  private:
    struct DeviceEntry {
        uint16_t address = 0;
        i2c_master_dev_handle_t handle = nullptr;
    };

    std::expected<DeviceHandle, ReturnCode>
    _storeDevice(uint16_t address, i2c_master_dev_handle_t handle) {
        // NOLINTNEXTLINE(bugprone-too-small-loop-variable)
        for (uint8_t index = 0; index < _devices.size(); ++index) {
            if (_devices[index].handle == nullptr) {
                _devices[index] = DeviceEntry{
                    .address = address,
                    .handle = handle,
                };
                return DeviceHandle{.index = index};
            }
        }
        return std::unexpected(ERR(CoreError, Overflow));
    }

    std::expected<const DeviceEntry *, ReturnCode>
    _entryFor(DeviceHandle device) const {
        FAIL_IF(!_initialized, std::unexpected(ERR(CoreError, InvalidState)),
                "I2C master bus is not initialized");
        FAIL_IF(!device.valid() || device.index >= _devices.size(),
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "Invalid I2C device handle");
        const auto &entry = _devices[device.index];
        FAIL_IF(entry.handle == nullptr,
                std::unexpected(ERR(CoreError, InvalidState)),
                "I2C device handle is not active");
        return &entry;
    }

    std::expected<DeviceHandle, ReturnCode> _findDevice(uint16_t address) const {
        // NOLINTNEXTLINE(bugprone-too-small-loop-variable)
        for (uint8_t index = 0; index < _devices.size(); ++index) {
            if (_devices[index].handle != nullptr &&
                _devices[index].address == address) {
                return DeviceHandle{.index = index};
            }
        }
        return std::unexpected(ERR(CoreError, NotFound));
    }

    i2c_master_bus_handle_t _bus = nullptr;
    uint32_t _clockHz = 0;
    bool _initialized = false;
    std::array<DeviceEntry, maxDevicesPerMaster> _devices{};
};

struct Platform {
    using MasterBus = Totem::Wire::I2C::detail::platform::MasterBus;
};

} // namespace Totem::Wire::I2C::detail::platform
