#pragma once

#include "Concepts/Base.hh"
#include "Macros/internal/Error.hh"
#include "Macros/internal/Format.hh"
#include "Platform/PlatformSelect.hh"
#include "Types/Error.hh"
#include "Types/Logging.hh"
#include "esp_log.h"
#include <atomic>
#include <concepts>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

namespace Totem::LoggerSupport::detail {

template <class T>
concept IsLoggerBackend =
    requires(T &cls, const LogRecord &record, LogLevel level,
             std::optional<LogComponent> componentOpt, LogComponent component) {
        { cls.setLogLevel(level, componentOpt) } -> std::same_as<ReturnCode>;
        { cls.send(record) } -> std::same_as<ReturnCode>;
        { cls.loggingFor(level, componentOpt) } -> std::same_as<bool>;
        {
            cls.setComponentLogLevelDefault(component)
        } -> std::same_as<ReturnCode>;
    } &&
    IsNamedEntity<T>;

struct LoggerBackend {
    void *self = nullptr;

    ReturnCode (*setLogLevelHook)(void *, LogLevel level,
                                  std::optional<LogComponent> component) =
        nullptr;
    ReturnCode (*sendHook)(void *, const LogRecord &record) = nullptr;
    bool (*loggingForHook)(void *, LogLevel level,
                           std::optional<LogComponent> component) = nullptr;
    ReturnCode (*setComponentLogLevelDefaultHook)(
        void *, LogComponent component) = nullptr;

    ReturnCode
    setLogLevel(LogLevel level,
                std::optional<LogComponent> component = std::nullopt) const {
        return setLogLevelHook(self, level, component);
    }
    ReturnCode send(const LogRecord &record) const {
        return sendHook(self, record);
    }
    [[nodiscard]] bool
    loggingFor(LogLevel level,
               std::optional<LogComponent> component = std::nullopt) const {
        return loggingForHook(self, level, component);
    }
    ReturnCode setComponentLogLevelDefault(LogComponent component) const {
        return setComponentLogLevelDefaultHook(self, component);
    }

    template <class T>
        requires IsLoggerBackend<T>
    static LoggerBackend bind(T &obj) {
        return LoggerBackend{
            .self = std::addressof(obj),
            .setLogLevelHook =
                [](void *ptr, LogLevel level,
                   std::optional<LogComponent> component) -> ReturnCode {
                return static_cast<T *>(ptr)->setLogLevel(level, component);
            },
            .sendHook = [](void *ptr, const LogRecord &data) -> ReturnCode {
                return static_cast<T *>(ptr)->send(data);
            },
            .loggingForHook =
                [](void *ptr, LogLevel level,
                   std::optional<LogComponent> component) -> bool {
                return static_cast<T *>(ptr)->loggingFor(level, component);
            },
            .setComponentLogLevelDefaultHook =
                [](void *ptr, LogComponent component) -> ReturnCode {
                return static_cast<T *>(ptr)->setComponentLogLevelDefault(
                    component);
            },
        };
    }

    static LoggerBackend null() {
        return LoggerBackend{
            .self = nullptr,
            .setLogLevelHook = [](void *, LogLevel, std::optional<LogComponent>)
                -> ReturnCode { return OK(CoreError); },
            .sendHook = [](void *, const LogRecord &record) -> ReturnCode {
                // FIXME: Abstract
                ESP_EARLY_LOGE(MAGIC_CHR(record.component), "%s",
                               record.msg.data());
                return OK(CoreError);
            },
            .loggingForHook = [](void *, LogLevel, std::optional<LogComponent>)
                -> bool { return true; },
            .setComponentLogLevelDefaultHook = [](void *, LogComponent)
                -> ReturnCode { return OK(CoreError); },
        };
    }

    [[nodiscard]] bool validate() const {
        return self != nullptr && setLogLevelHook != nullptr &&
               sendHook != nullptr && loggingForHook != nullptr &&
               setComponentLogLevelDefaultHook != nullptr;
    }
};

class LoggingService {
  public:
    template <class T>
    static ReturnCode setBackend(T &backend)
        requires IsLoggerBackend<T>
    {
        auto loggerBackend = LoggerBackend::bind(backend);
        // FIXME: Include cycle - logger macros use Logger, FAIL uses logger
        // macros FAIL_IF(!loggerBackend.validate(), ERR(InvalidArgument),
        //         "Invalid logger backend provided: %s", backend.name);
        _backend = loggerBackend;
        return OK();
    }

    static ReturnCode setLogLevel(LogLevel level) {
        return _backend.setLogLevel(level);
    }

    static ReturnCode send(const LogRecord &record) {
        return _backend.send(record);
    }

    static bool
    loggingFor(LogLevel level,
               std::optional<LogComponent> component = std::nullopt) {
        return _backend.loggingFor(level, component);
    }

    static ReturnCode setComponentLogLevelDefault(LogComponent component) {
        return _backend.setComponentLogLevelDefault(component);
    }

    __attribute__((__format__(__printf__, 3, 0))) static ReturnCode
    logf(LogLevel level, LogComponent component, const char *format, ...) {
        if (!loggingFor(level, component)) {
            return OK();
        }
        va_list args;
        va_start(args, format);
        auto result = vlogf(level, component, format, args);
        va_end(args);
        return result;
    }

    __attribute__((__format__(__printf__, 3, 0))) static ReturnCode
    vlogf(LogLevel level, LogComponent component, const char *format,
          va_list args) {
        if (!loggingFor(level, component)) {
            return OK();
        }
        if (!_recordBusy.test_and_set(std::memory_order_acquire)) {
            auto &record = _scratchRecord;
            if (auto ret =
                    _formatRecord(record, level, component, format, args);
                !ret.ok()) {
                _recordBusy.clear(std::memory_order_release);
                return ret;
            }
            auto result = send(record);
            _recordBusy.clear(std::memory_order_release);
            return result;
        }

        LogRecord record{};
        if (auto ret = _formatRecord(record, level, component, format, args);
            !ret.ok()) {
            return ret;
        }
        return send(record);
    }

  private:
    __attribute__((__format__(__printf__, 4, 0))) static ReturnCode
    _formatRecord(LogRecord &record, LogLevel level, LogComponent component,
                  const char *format, va_list args) {
        if (format == nullptr) {
            return ERR(InvalidArgument);
        }

        record = LogRecord{};
        record.ts = static_cast<uint32_t>(::platform::get_tick());
        record.component = component;
        record.level = level;

        std::vsnprintf(record.msg.data(), record.msg.size(), format, args);
        return OK();
    }

    static inline LoggerBackend _backend = LoggerBackend::null();
    static inline std::atomic_flag _recordBusy = ATOMIC_FLAG_INIT;
    static inline LogRecord _scratchRecord{};
};

} // namespace Totem::LoggerSupport::detail

using Totem::LoggerSupport::detail::LoggingService;
