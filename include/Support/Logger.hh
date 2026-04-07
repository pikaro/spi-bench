#pragma once

#include "Concepts/Base.hh"
#include "Macros/internal/Error.hh"
#include "Types/Error.hh"
#include "Types/Logging.hh"
#include "esp_log.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <concepts>

namespace Totem::LoggerSupport::detail {

template <class T>
concept IsLoggerBackend =
    requires(T &cls, const LogRecord &record, LogLevel level) {
        { cls.setLogLevel(level) } -> std::same_as<ReturnCode>;
        { cls.send(record) } -> std::same_as<ReturnCode>;
    } && IsNamedEntity<T>;

struct LoggerBackend {
    void *self = nullptr;

    ReturnCode (*setLogLevelHook)(void *, LogLevel level) = nullptr;
    ReturnCode (*sendHook)(void *, const LogRecord &record) = nullptr;

    ReturnCode setLogLevel(LogLevel level) const {
        return setLogLevelHook(self, level);
    }
    ReturnCode send(const LogRecord &record) const {
        return sendHook(self, record);
    }

    template <class T>
        requires IsLoggerBackend<T>
    static LoggerBackend bind(T &obj) {
        return LoggerBackend{
            .self = std::addressof(obj),
            .setLogLevelHook = [](void *ptr, LogLevel level) -> ReturnCode {
                return static_cast<T *>(ptr)->setLogLevel(level);
            },
            .sendHook = [](void *ptr, const LogRecord &data) -> ReturnCode {
                return static_cast<T *>(ptr)->send(data);
            },
        };
    }

    static LoggerBackend null() {
        return LoggerBackend{
            .self = nullptr,
            .setLogLevelHook = [](void *, LogLevel) -> ReturnCode {
                return OK(CoreError);
            },
            .sendHook = [](void *, const LogRecord &record) -> ReturnCode {
                // FIXME: Abstract
                ESP_EARLY_LOGE(record.tag.data(), "%s", record.msg.data());
                return OK(CoreError);
            },
        };
    }

    [[nodiscard]] bool validate() const {
        return self != nullptr && setLogLevelHook != nullptr &&
               sendHook != nullptr;
    }
};

class Logger {
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

    static ReturnCode logf(LogLevel level, const char *tag, const char *format,
                           ...) {
        va_list args;
        va_start(args, format);
        auto result = vlogf(level, tag, format, args);
        va_end(args);
        return result;
    }

    static ReturnCode vlogf(LogLevel level, const char *tag, const char *format,
                            va_list args) {
        if (!_recordBusy.test_and_set(std::memory_order_acquire)) {
            auto &record = _scratchRecord;
            _formatRecord(record, level, tag, format, args);
            auto result = send(record);
            _recordBusy.clear(std::memory_order_release);
            return result;
        }

        LogRecord record{};
        _formatRecord(record, level, tag, format, args);
        return send(record);
    }

  private:
    static void _formatRecord(LogRecord &record, LogLevel level, const char *tag,
                              const char *format, va_list args) {
        record = LogRecord{};
        record.ts = static_cast<uint32_t>(::platform::get_tick());

        const char *safeTag = tag != nullptr ? tag : "???";
        std::strncpy(record.tag.data(), safeTag, record.tag.size() - 1);
        record.tag[record.tag.size() - 1] = '\0';
        record.level = level;

        const char *safeFormat = format != nullptr ? format : "";
        std::vsnprintf(record.msg.data(), record.msg.size(), safeFormat, args);
    }

    static inline LoggerBackend _backend = LoggerBackend::null();
    static inline std::atomic_flag _recordBusy = ATOMIC_FLAG_INIT;
    static inline LogRecord _scratchRecord{};

    using DefaultError = CoreError;
};

} // namespace Totem::LoggerSupport::detail

using Logger = Totem::LoggerSupport::detail::Logger;
