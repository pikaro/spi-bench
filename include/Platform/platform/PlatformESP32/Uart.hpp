#pragma once

#include "Macros/Facade.hpp"
#include "StaticConfig/Uart.hpp"
#include "Types/Error.hpp"
#include "driver/uart.h"
#include "freertos/projdefs.h"
#include "hal/uart_types.h"
#include "soc/clk_tree_defs.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace platform {

struct Uart {
    static ReturnCode init() {
        const uart_config_t uart_config = {
            .baud_rate = ::UartConfig::baudRate,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 122,
            .source_clk = UART_SCLK_DEFAULT,
            .flags =
                {
                    .allow_pd = 0,
                    .backup_before_sleep = 0,
                },
        };

        FAIL_IF_ESP(uart_driver_install(_uartNumber, 2048, 0, 0, nullptr, 0),
                    ERR(OperationFailed), "Failed to install UART driver");
        FAIL_IF_ESP(uart_param_config(_uartNumber, &uart_config),
                    ERR(OperationFailed),
                    "Failed to configure UART parameters");
        FAIL_IF_ESP(uart_set_pin(_uartNumber, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE),
                    ERR(OperationFailed), "Failed to set UART pins");
        return OK();
    }

    static ReturnCode write(const char *data, size_t len, bool flush = false) {
        auto ret = uart_write_bytes(_uartNumber, data, len);
        FAIL_IF(ret < 0, ERR(OperationFailed), "Failed to write to UART");
        if (flush) {
            auto waitRet = uart_wait_tx_done(_uartNumber, pdMS_TO_TICKS(100));
            FAIL_IF_ESP(waitRet, ERR(OperationFailed), "Failed to flush UART");
        }
        return OK();
    }

    static std::expected<size_t, ReturnCode> read(std::span<uint8_t> buffer) {
        if (buffer.empty()) {
            return std::unexpected(ERR(InvalidArgument));
        }

        size_t available = 0;
        uart_get_buffered_data_len(_uartNumber, &available);
        if (available == 0) {
            return std::unexpected(ERR(NotFound));
        }

        auto requested = std::min(available, buffer.size());

        auto ret = uart_read_bytes(_uartNumber, buffer.data(), requested,
                                   pdMS_TO_TICKS(100));

        FAIL_IF(ret < 0, std::unexpected(ERR(OperationFailed)),
                "Failed to read from UART");

        if (ret == 0) {
            return std::unexpected(ERR(NotFound));
        }

        return static_cast<size_t>(ret);
    }

    static ReturnCode deinit() {
        FAIL_IF_ESP(uart_driver_delete(_uartNumber), ERR(OperationFailed),
                    "Failed to delete UART driver");
        return OK();
    }

  private:
    static_assert(::UartConfig::uartNumber < UART_NUM_MAX,
                  "UART number must be less than UART_NUM_MAX");
    static constexpr uart_port_t _uartNumber =
        static_cast<uart_port_t>(::UartConfig::uartNumber);
};

} // namespace platform
