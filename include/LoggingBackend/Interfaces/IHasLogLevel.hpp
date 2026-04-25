#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include <optional>

namespace Totem::LoggingBackend {

struct IHasLogLevel {
    virtual ~IHasLogLevel() = default;

    virtual ReturnCode
    setLogLevel(LogLevel level,
                std::optional<LogComponent> component = std::nullopt) = 0;
    [[nodiscard]] virtual bool
    loggingFor(LogLevel level,
               std::optional<LogComponent> component = std::nullopt) const = 0;
    virtual ReturnCode setComponentLogLevelDefault(LogComponent component) = 0;
};

} // namespace Totem::LoggingBackend
