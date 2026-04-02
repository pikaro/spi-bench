#pragma once

#include "Macros/Facade.hh"
#include "StaticConfig/Uart.hh"
#include "Types/Error.hh"
#include "driver/uart.h"
#include "freertos/projdefs.h"
#include "hal/uart_types.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace Totem::CommandBackend::detail::platform {

struct Platform {
    static std::expected<size_t, ReturnCode>
    uart_read(std::span<uint8_t> buffer) {
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

  private:
    static_assert(::UartConfig::uartNumber < UART_NUM_MAX,
                  "UART number must be less than UART_NUM_MAX");
    static constexpr uart_port_t _uartNumber =
        static_cast<uart_port_t>(::UartConfig::uartNumber);

    using DefaultError = CoreError;
};

} // namespace Totem::CommandBackend::detail::platform
