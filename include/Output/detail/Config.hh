#pragma once

#include "Common.hh"

#include "TaskController/detail/Config.hh"
#include "Types/Logging.hh"
#include <algorithm>
#include <array>

namespace Totem::Output::detail {

struct AggregatorConfig {
    size_t ringBufferSize = 100;
    LogLevel defaultLogLevel = LogLevel::Info;
    ::platform::Tick sendTimeoutMs = 100;
    ::platform::Tick receiveTimeoutMs = 10;

    static constexpr size_t maxSinks = 3;

    static constexpr TaskController::detail::Config task{
        .name = "Output",
        .stackSize = 4096,
        .intervalMs = 10,
    };

    [[nodiscard]] bool validate() const {
        return ringBufferSize > 0 && sendTimeoutMs > 0;
    }
};

static constexpr auto supportedBaudRates = std::to_array<int>({
    9600,
    19200,
    38400,
    57600,
    115200,
#ifdef PLATFORM_ESP32S3
    921600,
#endif
});

struct UartConfig {
    int baudRate = 115200;
    bool flush = true;

    static constexpr uint8_t uartNumber = 0;

    [[nodiscard]] bool validate() const {
        FAIL_IF(std::ranges::find(supportedBaudRates, baudRate) ==
                    supportedBaudRates.end(),
                false, "Unsupported baud rate %d for %s", baudRate);
        return true;
    }
};

} // namespace Totem::Output::detail
