#pragma once

#include "Macros/Facade.hh"
#include "Platform/PlatformSelect.hh"
#include "StaticConfig/Uart.hh"
#include "TaskController/Interfaces/Config.hh"
#include "Types/Logging.hh"
#include <algorithm>
#include <array>
#include <cstddef>

namespace Totem::Output {

struct AggregatorConfig {
    size_t ringBufferSize = 100;
    LogLevel defaultLogLevel = LogLevel::Info;
    ::platform::Tick sendTimeoutMs = 100;
    ::platform::Tick receiveTimeoutMs = 10;

    [[nodiscard]] bool validate() const {
        return ringBufferSize > 0 && sendTimeoutMs > 0;
    }

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
#ifdef PLATFORM_ESP32S3
    921600,
#endif
});

struct UartConfig {
    bool flush = true;

    [[nodiscard]] static bool validate() {
        FAIL_IF(std::ranges::find(supportedBaudRates, ::UartConfig::baudRate) ==
                    supportedBaudRates.end(),
                false, "Unsupported baud rate %d for %s",
                ::UartConfig::baudRate);
        return true;
    }
};

} // namespace Totem::Output
