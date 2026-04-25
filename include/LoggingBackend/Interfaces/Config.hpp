#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "StaticConfig/Uart.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include <algorithm>
#include <array>
#include <cstddef>

namespace Totem::LoggingBackend {

struct AggregatorConfig {
    size_t ringBufferSize = 100;
    LogLevel defaultLogLevel = LogLevel::Info;
    ::platform::Tick sendTimeoutMs = 0;
    ::platform::Tick receiveTimeoutMs = 5;

    [[nodiscard]] bool validate() const { return ringBufferSize > 0; }

    Totem::TaskController::Config task{
        .name = "Output",
        .stackSize = 4096,
        .intervalMs = 10,
    };
};

static constexpr auto supportedBaudRates = std::to_array<int>({
    9600,
    19200,
    38400,
    57600,
    115200,
    921600,
});

struct UartConfig {
    // Logging tolerates line delay, so prefer the driver's buffered TX path by
    // default and avoid forcing a full wire drain on every record.
    bool flush = false;

    [[nodiscard]] static bool validate() {
        FAIL_IF(std::ranges::find(supportedBaudRates, ::UartConfig::baudRate) ==
                    supportedBaudRates.end(),
                false, "Unsupported baud rate %d for %s",
                ::UartConfig::baudRate);
        return true;
    }
};

} // namespace Totem::LoggingBackend
