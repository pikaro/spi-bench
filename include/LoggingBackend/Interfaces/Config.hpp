#pragma once

#include "Platform/PlatformSelect.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include <cstddef>

namespace Totem::LoggingBackend {

struct AggregatorConfig {
    size_t ringBufferSize = 100;
    ::platform::Tick sendTimeoutMs = 0;
    ::platform::Tick receiveTimeoutMs = 5;

    [[nodiscard]] bool validate() const { return ringBufferSize > 0; }

    Totem::TaskController::Config task{
        .name = "Output",
        .stackSize = 4096,
        .intervalMs = 10,
    };
};

struct ConsoleConfig {
    // Logging tolerates line delay, so prefer the driver's buffered TX path by
    // default and avoid forcing a full wire drain on every record.
    bool flush = false;

    [[nodiscard]] static bool validate() { return true; }
};

} // namespace Totem::LoggingBackend
