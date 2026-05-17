#pragma once

#include "LoggingBackend/Interfaces/IHasLogLevel.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/internal/Error.hpp"
#include "Macros/internal/Format.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Services/Clock.hpp"
#include "Services/StatusLed.hpp"
#include "StaticConfig/Logging.hpp"
#include "Types/Error.hpp"
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <utility>

namespace Totem::LoggingBackend::detail {

struct ILogger : public IHasLogLevel {
    virtual ~ILogger() = default;

    virtual ReturnCode send(const LogRecord &record) = 0;
};

struct NullLogger : public ILogger {
    ReturnCode setLogLevel(LogLevel /*unused*/,
                           std::optional<LogComponent> /*unused*/) override {
        return OK(CoreError);
    }

    ReturnCode send(const LogRecord &record) override {
        ::platform::early_log_error(MAGIC_CHR(record.component),
                                    record.msg.data());
        return OK(CoreError);
    }

    [[nodiscard]] bool
    loggingFor(LogLevel /*unused*/,
               std::optional<LogComponent> /*unused*/) const override {
        return true;
    }

    ReturnCode setComponentLogLevelDefault(LogComponent /*unused*/) override {
        return OK(CoreError);
    }
};

static inline NullLogger nullLogger{};

} // namespace Totem::LoggingBackend::detail

template <typename... Levels>
consteval auto make_logging_minimum_levels(Levels... levels) {
    static_assert(sizeof...(levels) == magic_enum::enum_count<LogComponent>(),
                  "LogComponent changed; update minimum log level mapping");
    return std::array<LogLevel, sizeof...(levels)>{levels...};
}

template <size_t... Indices>
consteval bool minimum_log_component_values_are_dense(
    std::index_sequence<Indices...> /*unused*/) {
    constexpr auto values = magic_enum::enum_values<LogComponent>();
    return ((static_cast<size_t>(values[Indices]) == Indices) && ...);
}

static_assert(
    minimum_log_component_values_are_dense(
        std::make_index_sequence<magic_enum::enum_count<LogComponent>()>{}),
    "LogComponent values must remain contiguous from zero");

inline constexpr auto loggingMinimumLevels = make_logging_minimum_levels(
    LoggingMinimumLevel::defaultMinimum, LoggingMinimumLevel::system,
    LoggingMinimumLevel::monitoring, LoggingMinimumLevel::metrics,
    LoggingMinimumLevel::pubSub, LoggingMinimumLevel::command,
    LoggingMinimumLevel::taskControllerRegistry, LoggingMinimumLevel::output,
    LoggingMinimumLevel::rs485, LoggingMinimumLevel::spi,
    LoggingMinimumLevel::clock, LoggingMinimumLevel::ledPwm,
    LoggingMinimumLevel::statusLed, LoggingMinimumLevel::input,
    LoggingMinimumLevel::esp, LoggingMinimumLevel::audio);

static constexpr LogLevel logging_minimum_for(LogComponent component) {
    auto index = static_cast<size_t>(component);
    if (index >= loggingMinimumLevels.size()) {
        return LoggingMinimumLevel::defaultMinimum;
    }
    return loggingMinimumLevels[index];
}

static constexpr bool static_logging_for(LogLevel level,
                                         LogComponent component) {
    return static_cast<uint8_t>(level) >=
           static_cast<uint8_t>(logging_minimum_for(component));
}

class LoggingService {
    using ILogger = Totem::LoggingBackend::detail::ILogger;

  public:
    static void set(ILogger &backend) {
        // FIXME: Include cycle - logger macros use Logger, FAIL uses logger
        // macros FAIL_IF(!loggerBackend.validate(), ERR(InvalidArgument),
        //         "Invalid logger backend provided: %s", backend.name);
        _backend.store(&backend, std::memory_order_release);
    }

    static ILogger &get() { return *_backend.load(std::memory_order_acquire); }

    [[nodiscard]] static bool backendConfigured() {
        return _backend.load(std::memory_order_acquire) !=
               &Totem::LoggingBackend::detail::nullLogger;
    }

    [[nodiscard]] static bool
    loggingFor(LogLevel level,
               std::optional<LogComponent> component = std::nullopt) {
        if (component && !static_logging_for(level, *component)) {
            return false;
        }
        return get().loggingFor(level, component);
    }

    __attribute__((__format__(__printf__, 3, 0))) static ReturnCode
    logf(LogLevel level, LogComponent component, const char *format, ...) {
        if (!loggingFor(level, component)) {
            return OK();
        }
        va_list args;
        va_start(args, format);
        auto result =
            vlogfSite(level, component, logUnknownSiteId, format, args);
        va_end(args);
        return result;
    }

    __attribute__((__format__(__printf__, 3, 0))) static ReturnCode
    vlogf(LogLevel level, LogComponent component, const char *format,
          va_list args) {
        return vlogfSite(level, component, logUnknownSiteId, format, args);
    }

    __attribute__((__format__(__printf__, 4, 0))) static ReturnCode
    logfSite(LogLevel level, LogComponent component, LogSiteId siteId,
             const char *format, ...) {
        if (!loggingFor(level, component)) {
            return OK();
        }
        va_list args;
        va_start(args, format);
        auto result = vlogfSite(level, component, siteId, format, args);
        va_end(args);
        return result;
    }

    __attribute__((__format__(__printf__, 4, 0))) static ReturnCode
    vlogfSite(LogLevel level, LogComponent component, LogSiteId siteId,
              const char *format, va_list args) {
        if (!loggingFor(level, component)) {
            return OK();
        }
        if (!_recordBusy.test_and_set(std::memory_order_acquire)) {
            auto &record = _scratchRecord;
            if (auto ret =
                    _formatRecord(record, level, component, siteId, format,
                                  args);
                !ret.ok()) {
                _recordBusy.clear(std::memory_order_release);
                return ret;
            }
            auto result = get().send(record);
            if (result.ok() && level == LogLevel::Error) {
                (void)StatusLedService::recordLogError();
            }
            _recordBusy.clear(std::memory_order_release);
            return result;
        }

        LogRecord record{};
        if (auto ret =
                _formatRecord(record, level, component, siteId, format, args);
            !ret.ok()) {
            return ret;
        }
        auto result = get().send(record);
        if (result.ok() && level == LogLevel::Error) {
            (void)StatusLedService::recordLogError();
        }
        return result;
    }

  private:
    __attribute__((__format__(__printf__, 5, 0))) static ReturnCode
    _formatRecord(LogRecord &record, LogLevel level, LogComponent component,
                  LogSiteId siteId, const char *format, va_list args) {
        if (format == nullptr) {
            return ERR(InvalidArgument);
        }

        record = LogRecord{};
        record.ts = ::platform::get_time();
        record.tsSynced = ClockService::get().nowMs();
        record.siteId = siteId;
        record.component = component;
        record.level = level;

        std::vsnprintf(record.msg.data(), record.msg.size(), format, args);
        return OK();
    }

    static inline std::atomic<ILogger *> _backend{
        &Totem::LoggingBackend::detail::nullLogger};
    static inline std::atomic_flag _recordBusy = ATOMIC_FLAG_INIT;
    static inline LogRecord _scratchRecord{};
};
