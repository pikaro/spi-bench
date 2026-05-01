// IWYU pragma: private

#pragma once

#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include "Wire/Spi/Interfaces/MasterConfig.hpp"
#include "Wire/Spi/Interfaces/SlaveConfig.hpp"
#include "Wire/Spi/detail/Types.hpp"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "driver/spi_slave.h"
#include "freertos/projdefs.h"
#include "hal/spi_types.h"
#include "soc/soc_caps.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <expected>

namespace Totem::Wire::Spi::detail::platform {

using SpiSlaveCompletionCallback = void (*)(void *owner);

inline int pinValue(Pin pin) {
    return static_cast<int>(static_cast<uint8_t>(pin));
}

inline std::expected<spi_host_device_t, ReturnCode> hostFor(BusId busId) {
    switch (busId) {
    case BusId::Bus2:
        return SPI2_HOST;
    case BusId::Bus3:
#if SOC_SPI_PERIPH_NUM > 2
        return SPI3_HOST;
#else
        return std::unexpected(ERR(CoreError, InvalidArgument));
#endif
    default:
        return std::unexpected(ERR(CoreError, InvalidArgument));
    }
}

class SpiMasterBus {
  public:
    DELETE_COPY(SpiMasterBus)
    DELETE_MOVE(SpiMasterBus)

    SpiMasterBus() = default;

    ReturnCode init(const MasterBusConfig &config) {
        FAIL_IF(_initialized, ERR(CoreError, InvalidState),
                "SPI master bus already initialized");
        auto hostResult = hostFor(config.busId);
        if (!hostResult) {
            return hostResult.error();
        }

        spi_bus_config_t busConfig{};
        busConfig.mosi_io_num = pinValue(*config.pins.mosiPin);
        busConfig.miso_io_num = pinValue(*config.pins.misoPin);
        busConfig.sclk_io_num = pinValue(*config.pins.sclkPin);
        busConfig.quadwp_io_num = -1;
        busConfig.quadhd_io_num = -1;
        busConfig.data4_io_num = -1;
        busConfig.data5_io_num = -1;
        busConfig.data6_io_num = -1;
        busConfig.data7_io_num = -1;
        busConfig.max_transfer_sz = static_cast<int>(config.maxTransferSize);
        busConfig.flags = SPICOMMON_BUSFLAG_MASTER;

        FAIL_IF_PLATFORM_FWD(
            spi_bus_initialize(*hostResult, &busConfig, SPI_DMA_CH_AUTO),
            "Failed to initialize SPI master bus");
        _host = *hostResult;
        _initialized = true;
        return OK();
    }

    std::expected<DeviceHandle, ReturnCode>
    addDevice(const MasterDeviceConfig &config) {
        FAIL_IF(!_initialized, std::unexpected(ERR(CoreError, InvalidState)),
                "Cannot add SPI device before bus init");

        spi_device_interface_config_t deviceConfig{};
        deviceConfig.mode = static_cast<uint8_t>(config.mode);
        deviceConfig.clock_speed_hz = static_cast<int>(config.clockHz);
        deviceConfig.input_delay_ns = config.inputDelayNs;
        deviceConfig.spics_io_num = pinValue(*config.csPin);
        deviceConfig.queue_size = config.queueSize;
        if (config.bitOrder == BitOrder::LsbFirst) {
            deviceConfig.flags |= SPI_DEVICE_BIT_LSBFIRST;
        }

        spi_device_handle_t handle = nullptr;
        FAIL_IF_PLATFORM_FWD_UNEXPECTED(
            spi_bus_add_device(_host, &deviceConfig, &handle),
            "Failed to add SPI device");
        auto stored = _storeDevice(handle);
        if (!stored) {
            (void)spi_bus_remove_device(handle);
            return std::unexpected(stored.error());
        }
        return *stored;
    }

    ReturnCode removeDevice(DeviceHandle device) {
        FAIL_IF(!_initialized, ERR(CoreError, InvalidState),
                "Cannot remove SPI device before bus init");
        auto handleResult = _deviceFor(device);
        if (!handleResult) {
            return handleResult.error();
        }

        FAIL_IF_PLATFORM_FWD(spi_bus_remove_device(*handleResult),
                             "Failed to remove SPI device");
        _eraseDevice(device);
        return OK();
    }

    ReturnCode transfer(DeviceHandle device, const Transfer &transfer) const {
        FAIL_IF(!_initialized, ERR(CoreError, InvalidState),
                "Cannot transfer before SPI master bus init");
        FAIL_IF(!transfer.validate(), ERR(CoreError, InvalidArgument),
                "Invalid SPI transfer");

        spi_transaction_t transaction{};
        transaction.length = transfer.clockedSize() * 8;
        transaction.rxlength = transfer.rxBuffer.size() * 8;
        transaction.tx_buffer =
            transfer.txBuffer.empty() ? nullptr : transfer.txBuffer.data();
        transaction.rx_buffer =
            transfer.rxBuffer.empty() ? nullptr : transfer.rxBuffer.data();

        auto handleResult = _deviceFor(device);
        if (!handleResult) {
            return handleResult.error();
        }
        return ::platform::map_platform_error(
            spi_device_transmit(*handleResult, &transaction));
    }

    ReturnCode deinit() {
        if (!_initialized) {
            return OK();
        }
        for (auto &device : _devices) {
            if (device != nullptr) {
                (void)spi_bus_remove_device(device);
                device = nullptr;
            }
        }
        FAIL_IF_PLATFORM_FWD(spi_bus_free(_host),
                             "Failed to free SPI master bus");
        _initialized = false;
        return OK();
    }

  private:
    std::expected<DeviceHandle, ReturnCode>
    _storeDevice(spi_device_handle_t handle) {
        // NOLINTNEXTLINE(bugprone-too-small-loop-variable)
        for (uint8_t index = 0; index < _devices.size(); index++) {
            if (_devices[index] == nullptr) {
                _devices[index] = handle;
                return DeviceHandle{.index = index};
            }
        }
        return std::unexpected(ERR(CoreError, Overflow));
    }

    void _eraseDevice(DeviceHandle handle) {
        if (handle.valid() && handle.index < _devices.size()) {
            _devices[handle.index] = nullptr;
        }
    }

    std::expected<spi_device_handle_t, ReturnCode>
    _deviceFor(DeviceHandle handle) const {
        FAIL_IF(!handle.valid() || handle.index >= _devices.size(),
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "Invalid SPI device handle");
        auto *device = _devices[handle.index];
        FAIL_IF(device == nullptr,
                std::unexpected(ERR(CoreError, InvalidState)),
                "SPI device handle is not active");
        return device;
    }

    spi_host_device_t _host = SPI2_HOST;
    bool _initialized = false;
    std::array<spi_device_handle_t, 4> _devices{};
};

class SpiSlaveDevice {
  public:
    DELETE_COPY(SpiSlaveDevice)
    DELETE_MOVE(SpiSlaveDevice)

    SpiSlaveDevice() = default;

    void registerCompletionCallback(void *owner,
                                    SpiSlaveCompletionCallback callback) {
        _completionOwner = owner;
        _completionCallback = callback;
    }

    ReturnCode init(const SlaveConfig &config) {
        FAIL_IF(_initialized, ERR(CoreError, InvalidState),
                "SPI slave already initialized");
        auto hostResult = hostFor(config.busId);
        if (!hostResult) {
            return hostResult.error();
        }

        spi_bus_config_t busConfig{};
        busConfig.mosi_io_num = pinValue(*config.pins.mosiPin);
        busConfig.miso_io_num = pinValue(*config.pins.misoPin);
        busConfig.sclk_io_num = pinValue(*config.pins.sclkPin);
        busConfig.quadwp_io_num = -1;
        busConfig.quadhd_io_num = -1;
        busConfig.data4_io_num = -1;
        busConfig.data5_io_num = -1;
        busConfig.data6_io_num = -1;
        busConfig.data7_io_num = -1;
        busConfig.max_transfer_sz = static_cast<int>(config.maxTransferSize);
        busConfig.flags = SPICOMMON_BUSFLAG_SLAVE;

        spi_slave_interface_config_t slaveConfig{};
        slaveConfig.spics_io_num = pinValue(*config.csPin);
        slaveConfig.flags = 0;
        slaveConfig.queue_size = config.queueSize;
        slaveConfig.mode = static_cast<uint8_t>(config.mode);
        slaveConfig.post_setup_cb = _onTransactionSetup;
        slaveConfig.post_trans_cb = _onTransactionComplete;
        if (config.bitOrder == BitOrder::LsbFirst) {
            slaveConfig.flags |= SPI_SLAVE_BIT_LSBFIRST;
        }

        FAIL_IF_PLATFORM_FWD(spi_slave_initialize(*hostResult, &busConfig,
                                                  &slaveConfig,
                                                  SPI_DMA_CH_AUTO),
                             "Failed to initialize SPI slave");
        _host = *hostResult;
        _initialized = true;
        return OK();
    }

    ReturnCode queueTransfer(const Transfer &transfer) {
        FAIL_IF(!_initialized, ERR(CoreError, InvalidState),
                "Cannot queue SPI transfer before slave init");
        FAIL_IF(_queued, ERR(CoreError, InvalidState),
                "SPI slave transfer already queued");
        FAIL_IF(!transfer.validate(), ERR(CoreError, InvalidArgument),
                "Invalid SPI transfer");

        _startedAtUs.store(0, std::memory_order_release);
        _completedAtUs.store(0, std::memory_order_release);
        _transaction = {};
        _transaction.length = transfer.clockedSize() * 8;
        _transaction.user = this;
        _transaction.tx_buffer =
            transfer.txBuffer.empty() ? nullptr : transfer.txBuffer.data();
        _transaction.rx_buffer =
            transfer.rxBuffer.empty() ? nullptr : transfer.rxBuffer.data();

        FAIL_IF_PLATFORM_FWD(
            spi_slave_queue_trans(_host, &_transaction,
                                  pdMS_TO_TICKS(transfer.timeoutMs)),
            "Failed to queue SPI slave transfer");
        _queued = true;
        return OK();
    }

    std::expected<TransferResult, ReturnCode> waitTransfer(uint32_t timeoutMs) {
        FAIL_IF(!_initialized, std::unexpected(ERR(CoreError, InvalidState)),
                "Cannot wait for SPI transfer before slave init");
        FAIL_IF(!_queued, std::unexpected(ERR(CoreError, InvalidState)),
                "No SPI slave transfer queued");

        spi_slave_transaction_t *transaction = nullptr;
        auto ret = ::platform::map_platform_error(spi_slave_get_trans_result(
            _host, &transaction, pdMS_TO_TICKS(timeoutMs)));
        if (!ret.ok()) {
            return std::unexpected(ret);
        }

        _queued = false;
        return TransferResult{
            .bytesTransferred =
                transaction == nullptr ? 0 : transaction->trans_len / 8,
            .startedAtUs = _startedAtUs.load(std::memory_order_acquire),
            .completedAtUs = _completedAtUs.load(std::memory_order_acquire),
        };
    }

    ReturnCode transmit(const Transfer &transfer) {
        FAIL_IF_ERR_FWD(queueTransfer(transfer),
                        "Failed to queue SPI slave transfer");
        auto result = waitTransfer(transfer.timeoutMs);
        if (!result) {
            return result.error();
        }
        return OK();
    }

    ReturnCode deinit() {
        if (!_initialized) {
            return OK();
        }
        registerCompletionCallback(nullptr, nullptr);
        FAIL_IF_PLATFORM_FWD(spi_slave_free(_host), "Failed to free SPI slave");
        _initialized = false;
        _queued = false;
        return OK();
    }

  private:
    static void _onTransactionSetup(spi_slave_transaction_t *transaction) {
        if (transaction == nullptr || transaction->user == nullptr) {
            return;
        }
        auto *self = static_cast<SpiSlaveDevice *>(transaction->user);
        self->_startedAtUs.store(::platform::get_time_us(),
                                 std::memory_order_release);
    }

    static void _onTransactionComplete(spi_slave_transaction_t *transaction) {
        if (transaction == nullptr || transaction->user == nullptr) {
            return;
        }
        auto *self = static_cast<SpiSlaveDevice *>(transaction->user);
        self->_completedAtUs.store(::platform::get_time_us(),
                                   std::memory_order_release);
        if (self->_completionCallback == nullptr) {
            return;
        }
        self->_completionCallback(self->_completionOwner);
    }

    spi_host_device_t _host = SPI2_HOST;
    bool _initialized = false;
    bool _queued = false;
    spi_slave_transaction_t _transaction{};
    std::atomic<int64_t> _startedAtUs{0};
    std::atomic<int64_t> _completedAtUs{0};
    void *_completionOwner = nullptr;
    SpiSlaveCompletionCallback _completionCallback = nullptr;
};

struct Platform {
    using SpiMasterBus = Totem::Wire::Spi::detail::platform::SpiMasterBus;
    using SpiSlaveDevice = Totem::Wire::Spi::detail::platform::SpiSlaveDevice;
};

} // namespace Totem::Wire::Spi::detail::platform
