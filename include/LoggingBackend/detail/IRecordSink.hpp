#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <optional>
#include <string_view>

namespace Totem::LoggingBackend {

struct IRecordSink {
    virtual ~IRecordSink() = default;

    [[nodiscard]] virtual bool
    loggingFor(LogLevel level,
               std::optional<LogComponent> component = std::nullopt) const = 0;
    [[nodiscard]] virtual std::string_view displayName() const = 0;
    virtual ReturnCode write(const LogRecord &record) = 0;
};

} // namespace Totem::LoggingBackend
