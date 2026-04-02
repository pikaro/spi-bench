#pragma once

#include "Common.hh"
#include "StaticConfig/Uart.hh"
#include "driver/uart.h"
#include "hal/uart_types.h"
#include "soc/clk_tree_defs.h"
#include <cstddef>

namespace Totem::Output::detail::platform {

struct Platform {
    static ReturnCode uart_init() {
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

    static ReturnCode uart_write(const char *data, size_t len,
                                 bool flush = false) {
        auto ret = uart_write_bytes(_uartNumber, data, len);
        FAIL_IF(ret < 0, ERR(OperationFailed), "Failed to write to UART");
        if (flush) {
            auto waitRet = uart_wait_tx_done(_uartNumber, pdMS_TO_TICKS(100));
            FAIL_IF_ESP(waitRet, ERR(OperationFailed), "Failed to flush UART");
        }
        return OK();
    }

    static ReturnCode uart_deinit() {
        FAIL_IF_ESP(uart_driver_delete(_uartNumber), ERR(OperationFailed),
                    "Failed to delete UART driver");
        return OK();
    }

  private:
    using DefaultError = CoreError;

    static_assert(::UartConfig::uartNumber < UART_NUM_MAX,
                  "UART number must be less than UART_NUM_MAX");
    static constexpr uart_port_t _uartNumber =
        static_cast<uart_port_t>(::UartConfig::uartNumber);
};

} // namespace Totem::Output::detail::platform
