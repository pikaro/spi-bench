#pragma once

#include "LoggingBackend/Interfaces/IHasLogLevel.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "StaticConfig/Logging.hpp"
#include "Types/Error.hpp"
#include <magic_enum/magic_enum.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace Totem::LoggingBackend::detail {

template <typename... Levels>
consteval auto make_default_log_levels(Levels... levels) {
    static_assert(sizeof...(levels) == magic_enum::enum_count<LogComponent>(),
                  "LogComponent changed; update default log level mapping");
    return std::array<std::optional<LogLevel>, sizeof...(levels)>{
        std::optional<LogLevel>{levels}...};
}

template <size_t... Indices>
consteval bool
log_component_values_are_dense(std::index_sequence<Indices...> /*unused*/) {
    constexpr auto values = magic_enum::enum_values<LogComponent>();
    return ((static_cast<size_t>(values[Indices]) == Indices) && ...);
}

static_assert(log_component_values_are_dense(
                  std::make_index_sequence<
                      magic_enum::enum_count<LogComponent>()>{}),
              "LogComponent values must remain contiguous from zero");

inline constexpr auto componentDefaultLogLevels = make_default_log_levels(
    LoggingDefaultLevel::defaultLevel, LoggingDefaultLevel::system,
    LoggingDefaultLevel::monitoring, LoggingDefaultLevel::metrics,
    LoggingDefaultLevel::pubSub, LoggingDefaultLevel::command,
    LoggingDefaultLevel::taskControllerRegistry, LoggingDefaultLevel::output,
    LoggingDefaultLevel::rs485, LoggingDefaultLevel::spi,
    LoggingDefaultLevel::clock, LoggingDefaultLevel::ledPwm,
    LoggingDefaultLevel::input);

static constexpr std::optional<LogLevel>
default_level_for(LogComponent component) {
    auto index = static_cast<size_t>(component);
    if (index >= componentDefaultLogLevels.size()) {
        return LoggingDefaultLevel::defaultLevel;
    }
    return componentDefaultLogLevels[index];
}

class HasLogLevel : public IHasLogLevel {
  public:
    explicit HasLogLevel(const char *ownerName) : _ownerName(ownerName) {
        for (size_t i = 0; i < _componentLogLevels.size(); ++i) {
            _componentLogLevels[i] =
                default_level_for(static_cast<LogComponent>(i));
        }
    }

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
    LogLevel _logLevel = LoggingDefaultLevel::defaultLevel;
    std::array<std::optional<LogLevel>, magic_enum::enum_count<LogComponent>()>
        _componentLogLevels{std::nullopt};
};

} // namespace Totem::LoggingBackend::detail
