#pragma once

#include "LoggingBackend/Interfaces/IHasLogLevel.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/internal/Error.hpp"
#include "Macros/internal/Format.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include "esp_log.h"
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

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
        // FIXME: Abstract
        ESP_EARLY_LOGE(MAGIC_CHR(record.component), "%s", record.msg.data());
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

class LoggingService {
    using ILogger = Totem::LoggingBackend::detail::ILogger;

  public:
    static void set(ILogger &backend) {
        // FIXME: Include cycle - logger macros use Logger, FAIL uses logger
        // macros FAIL_IF(!loggerBackend.validate(), ERR(InvalidArgument),
        //         "Invalid logger backend provided: %s", backend.name);
        _backend.store(&backend, std::memory_order_release);
    }

    static ILogger &get() {
        return *_backend.load(std::memory_order_acquire);
    }

    [[nodiscard]] static bool
    loggingFor(LogLevel level,
               std::optional<LogComponent> component = std::nullopt) {
        return get().loggingFor(level, component);
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
            auto result = get().send(record);
            _recordBusy.clear(std::memory_order_release);
            return result;
        }

        LogRecord record{};
        if (auto ret = _formatRecord(record, level, component, format, args);
            !ret.ok()) {
            return ret;
        }
        return get().send(record);
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

    static inline std::atomic<ILogger *> _backend{
        &Totem::LoggingBackend::detail::nullLogger};
    static inline std::atomic_flag _recordBusy = ATOMIC_FLAG_INIT;
    static inline LogRecord _scratchRecord{};
};
