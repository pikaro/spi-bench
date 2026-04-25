#pragma once

#include "LoggingBackend/Interfaces/IHasLogLevel.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Totem::LoggingBackend::detail {

class HasLogLevel : public IHasLogLevel {
  public:
    explicit HasLogLevel(const char *ownerName) : _ownerName(ownerName) {}

    [[nodiscard]] bool loggingFor(
        LogLevel level,
        std::optional<LogComponent> component = std::nullopt) const override {
        if (!component) {
            return level >= _logLevel;
        }
        if (static_cast<uint8_t>(*component) >= _componentLogLevels.size()) {
            _log_e("Invalid log component %u in %s",
                   static_cast<uint8_t>(*component), _ownerName);
            return true;
        }
        auto levelNum = static_cast<uint8_t>(level);
        auto componentLevel = static_cast<uint8_t>(
            _componentLogLevels[static_cast<size_t>(*component)].value_or(
                _logLevel));
        return levelNum >= componentLevel;
    }

    ReturnCode
    setLogLevel(LogLevel level,
                std::optional<LogComponent> component = std::nullopt) override {
        if (component) {
            if (static_cast<uint8_t>(*component) >=
                _componentLogLevels.size()) {
                return ERR(InvalidArgument);
            }
            _componentLogLevels[static_cast<size_t>(*component)] = level;
            _log_i("Set log level for component " SV_FMT " to " SV_FMT,
                   MAGIC_SV_ARG(*component), MAGIC_SV_ARG(level));
        } else {
            _logLevel = level;
            _log_i("Set default log level to " SV_FMT, MAGIC_SV_ARG(level));
        }
        return OK();
    }

    ReturnCode setComponentLogLevelDefault(LogComponent component) override {
        if (static_cast<uint8_t>(component) >= _componentLogLevels.size()) {
            return ERR(InvalidArgument);
        }
        _componentLogLevels[static_cast<size_t>(component)] = std::nullopt;
        return OK();
    }

  private:
    const char *const _ownerName;
    LogLevel _logLevel = LogLevel::Info;
    std::array<std::optional<LogLevel>, magic_enum::enum_count<LogComponent>()>
        _componentLogLevels{std::nullopt};
};

} // namespace Totem::LoggingBackend::detail
