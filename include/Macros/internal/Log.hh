#pragma once

#include "Base/Namespaces.hh" // IWYU pragma: export
#include "esp_log.h"
#include <array>           // IWYU pragma: export
#include <source_location> // IWYU pragma: export

#define LOG_LOC(msg, ...)                                                      \
    do {                                                                       \
        auto loc = std::source_location::current();                            \
        ESP_LOGE(TAG, "[%s:%d:%s] " msg, loc.file_name(), loc.line(),          \
                 loc.function_name(), ##__VA_ARGS__);                          \
    } while (0)

#define INTERNAL_LOG_IMPL(logLevel, logTag, logFormat, ...)                    \
    do {                                                                       \
        LogRecord rec{};                                                       \
        rec.ts = static_cast<uint32_t>(::platform::get_tick());                \
        strncpy(rec.tag.data(), logTag, rec.tag.size() - 1);                   \
        rec.tag[rec.tag.size() - 1] = '\0';                                    \
        rec.level = logLevel;                                                  \
        snprintf(rec.msg.data(), rec.msg.size(), logFormat, ##__VA_ARGS__);    \
        (void)Logger::send(rec);                                               \
    } while (0)

#define _log_v(format, ...)                                                    \
    INTERNAL_LOG_IMPL(LogLevel::Verbose, TAG, format, ##__VA_ARGS__)
#define _log_d(format, ...)                                                    \
    INTERNAL_LOG_IMPL(LogLevel::Debug, TAG, format, ##__VA_ARGS__)
#define _log_i(format, ...)                                                    \
    INTERNAL_LOG_IMPL(LogLevel::Info, TAG, format, ##__VA_ARGS__)
#define _log_w(format, ...)                                                    \
    INTERNAL_LOG_IMPL(LogLevel::Warning, TAG, format, ##__VA_ARGS__)
#define _log_e(format, ...)                                                    \
    INTERNAL_LOG_IMPL(LogLevel::Error, TAG, format, ##__VA_ARGS__)

#define _cls_log_v(format, ...)                                                \
    INTERNAL_LOG_IMPL(LogLevel::Verbose, _logTag, format, ##__VA_ARGS__)
#define _cls_log_d(format, ...)                                                \
    INTERNAL_LOG_IMPL(LogLevel::Debug, _logTag, format, ##__VA_ARGS__)
#define _cls_log_i(format, ...)                                                \
    INTERNAL_LOG_IMPL(LogLevel::Info, _logTag, format, ##__VA_ARGS__)
#define _cls_log_w(format, ...)                                                \
    INTERNAL_LOG_IMPL(LogLevel::Warning, _logTag, format, ##__VA_ARGS__)
#define _cls_log_e(format, ...)                                                \
    INTERNAL_LOG_IMPL(LogLevel::Error, _logTag, format, ##__VA_ARGS__)

#define _log_lv(level, format, ...)                                            \
    esp_log_level(level, tag, format, ##__va_args__)
