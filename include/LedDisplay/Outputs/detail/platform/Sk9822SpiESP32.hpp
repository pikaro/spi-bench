// IWYU pragma: private

#pragma once

#include "LedDisplay/Interfaces/OutputConfig.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "hal/spi_types.h"
#include <cstddef>
#include <cstdint>
#include <span>

namespace Totem::LedDisplay::Outputs::detail::platform {

class Sk9822SpiESP32 {
  public:
    Sk9822SpiESP32() = default;

    Sk9822SpiESP32(const Sk9822SpiESP32 &) = delete;
    Sk9822SpiESP32 &operator=(const Sk9822SpiESP32 &) = delete;
    Sk9822SpiESP32(Sk9822SpiESP32 &&) = delete;
    Sk9822SpiESP32 &operator=(Sk9822SpiESP32 &&) = delete;

    ReturnCode begin(const Sk9822OutputConfig &config,
                     std::span<const std::byte> dmaBuffer) {
        if (_initialized) {
            return ReturnCode::from(CoreError::InvalidState);
        }
        if (!config.validate() || dmaBuffer.empty() ||
            (reinterpret_cast<uintptr_t>(dmaBuffer.data()) % 4U) != 0U ||
            (dmaBuffer.size() % 4U) != 0U ||
            !esp_ptr_dma_capable(dmaBuffer.data()) ||
            !esp_ptr_dma_capable(dmaBuffer.data() + dmaBuffer.size() - 1U)) {
            return ReturnCode::from(CoreError::InvalidArgument);
        }

        spi_bus_config_t busConfig{};
        busConfig.mosi_io_num = pinValue(*config.dataPin);
        busConfig.miso_io_num = -1;
        busConfig.sclk_io_num = pinValue(*config.clockPin);
        busConfig.quadwp_io_num = -1;
        busConfig.quadhd_io_num = -1;
        busConfig.data4_io_num = -1;
        busConfig.data5_io_num = -1;
        busConfig.data6_io_num = -1;
        busConfig.data7_io_num = -1;
        busConfig.max_transfer_sz = static_cast<int>(dmaBuffer.size());
        busConfig.flags = SPICOMMON_BUSFLAG_MASTER;

        const auto spiHost = _host(config.host);
        auto ret = ::platform::map_platform_error(
            spi_bus_initialize(spiHost, &busConfig, SPI_DMA_CH_AUTO));
        if (!ret.ok()) {
            return ret;
        }
        _busInitialized = true;

        spi_device_interface_config_t deviceConfig{};
        deviceConfig.mode = 0;
        deviceConfig.clock_speed_hz = static_cast<int>(config.clockHz);
        deviceConfig.spics_io_num = -1;
        deviceConfig.queue_size = 1;

        ret = ::platform::map_platform_error(
            spi_bus_add_device(spiHost, &deviceConfig, &_device));
        if (!ret.ok()) {
            (void)spi_bus_free(spiHost);
            _busInitialized = false;
            return ret;
        }

        _spiHost = spiHost;
        _timeoutMs = config.transferTimeoutMs;
        _initialized = true;
        return ReturnCode::from(CoreError::Ok);
    }

    ReturnCode queue(std::span<const std::byte> dmaBuffer) {
        if (!_initialized || _device == nullptr) {
            return ReturnCode::from(CoreError::InvalidState);
        }
        if (dmaBuffer.empty() || (dmaBuffer.size() % 4U) != 0U ||
            (reinterpret_cast<uintptr_t>(dmaBuffer.data()) % 4U) != 0U ||
            !esp_ptr_dma_capable(dmaBuffer.data())) {
            return ReturnCode::from(CoreError::InvalidArgument);
        }
        if (_queued) {
            return ReturnCode::from(CoreError::InvalidState);
        }

        _transaction = {};
        _transaction.length = dmaBuffer.size() * 8U;
        _transaction.tx_buffer = dmaBuffer.data();
        auto ret = ::platform::map_platform_error(spi_device_queue_trans(
            _device, &_transaction, pdMS_TO_TICKS(_timeoutMs)));
        if (!ret.ok()) {
            return ret;
        }
        _queued = true;
        return ReturnCode::from(CoreError::Ok);
    }

    ReturnCode wait() {
        if (!_initialized || _device == nullptr) {
            return ReturnCode::from(CoreError::InvalidState);
        }
        if (!_queued) {
            return ReturnCode::from(CoreError::Ok);
        }

        spi_transaction_t *completed = nullptr;
        auto ret = ::platform::map_platform_error(spi_device_get_trans_result(
            _device, &completed, pdMS_TO_TICKS(_timeoutMs)));
        if (!ret.ok()) {
            return ret;
        }

        // A successful reap releases the DMA buffer even if ESP-IDF returns an
        // unexpected descriptor. Never leave the buffer permanently marked as
        // owned after the driver has completed the transaction.
        _queued = false;
        if (completed != &_transaction) {
            return ReturnCode::from(CoreError::InvalidResponse);
        }
        return ReturnCode::from(CoreError::Ok);
    }

    ReturnCode transmit(std::span<const std::byte> dmaBuffer) {
        auto ret = wait();
        if (!ret.ok()) {
            return ret;
        }
        ret = queue(dmaBuffer);
        if (!ret.ok()) {
            return ret;
        }
        return wait();
    }

    ReturnCode deinit() {
        if (!_initialized) {
            return ReturnCode::from(CoreError::Ok);
        }
        if (_queued) {
            auto ret = wait();
            if (!ret.ok()) {
                return ret;
            }
        }
        if (_device != nullptr) {
            auto ret =
                ::platform::map_platform_error(spi_bus_remove_device(_device));
            if (!ret.ok()) {
                return ret;
            }
            _device = nullptr;
        }
        if (_busInitialized) {
            auto ret = ::platform::map_platform_error(spi_bus_free(_spiHost));
            if (!ret.ok()) {
                return ret;
            }
            _busInitialized = false;
        }
        _initialized = false;
        return ReturnCode::from(CoreError::Ok);
    }

  private:
    [[nodiscard]] static int pinValue(Pin pin) {
        return static_cast<int>(static_cast<uint8_t>(pin));
    }

    [[nodiscard]] static constexpr spi_host_device_t _host(Sk9822SpiHost host) {
        switch (host) {
        case Sk9822SpiHost::Spi3:
            return SPI3_HOST;
        }
        return SPI3_HOST;
    }

    spi_device_handle_t _device = nullptr;
    spi_host_device_t _spiHost = SPI3_HOST;
    spi_transaction_t _transaction{};
    uint32_t _timeoutMs = 10;
    bool _busInitialized = false;
    bool _initialized = false;
    bool _queued = false;
};

} // namespace Totem::LedDisplay::Outputs::detail::platform
