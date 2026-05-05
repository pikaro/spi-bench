// IWYU pragma: private

#pragma once

// IWYU pragma: begin_exports

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Services/Logging.hpp"
#include "StaticConfig/Logging.hpp"
#include <array>
#include <source_location>
#include <string_view>

// IWYU pragma: end_exports

namespace Totem::LoggerBackend::detail {

constexpr uint32_t logSiteHashByte(uint32_t hash, uint8_t value) {
    return (hash ^ value) * 16777619U;
}

constexpr uint32_t logSiteHashString(uint32_t hash, const char *value) {
    if (value == nullptr) {
        return logSiteHashByte(hash, 0);
    }
    while (*value != '\0') {
        hash = logSiteHashByte(hash, static_cast<uint8_t>(*value));
        ++value;
    }
    return logSiteHashByte(hash, 0);
}

consteval LogSiteId logSiteId(const char *format, LogComponent component,
                              LogLevel level) {
    uint32_t hash = 2166136261U;
    hash = logSiteHashByte(hash, static_cast<uint8_t>(component));
    hash = logSiteHashByte(hash, static_cast<uint8_t>(level));
    hash = logSiteHashString(hash, format);
    return hash == logUnknownSiteId ? 1U : hash;
}

constexpr std::string_view logFileName(std::string_view path) {
    auto pos = path.find_last_of("/\\");
    return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

constexpr std::string_view logFunctionName(std::string_view function) {
    auto end = function.find('(');
    auto head = function.substr(0, end);

    auto operatorPos = head.rfind("operator ");
    auto start = head.rfind(' ');
    if (operatorPos != std::string_view::npos &&
        (start == std::string_view::npos || operatorPos > start)) {
        start = head.rfind(' ', operatorPos);
    }

    auto qualified =
        start == std::string_view::npos ? head : head.substr(start + 1);

    auto lastSep = qualified.rfind("::");
    if (lastSep == std::string_view::npos) {
        return qualified;
    }

    auto prevSep = qualified.rfind("::", lastSep - 1);
    if (prevSep == std::string_view::npos) {
        return qualified;
    }

    return qualified.substr(prevSep + 2);
}

} // namespace Totem::LoggerBackend::detail

#define LOG_LOC(msg, ...)                                                      \
    do {                                                                       \
        if constexpr (static_logging_for(LogLevel::Error, logComponent)) {     \
            auto loc = std::source_location::current();                        \
            auto _logFile =                                                    \
                Totem::LoggerBackend::detail::logFileName(loc.file_name());    \
            auto _logFunction = Totem::LoggerBackend::detail::logFunctionName( \
                loc.function_name());                                          \
            static constexpr LogSiteId _logSiteId =                            \
                Totem::LoggerBackend::detail::logSiteId(                       \
                    msg, logComponent, LogLevel::Error);                       \
            (void)LoggingService::logfSite(                                    \
                LogLevel::Error, logComponent, _logSiteId,                     \
                "[%.*s:%d:%.*s] " msg,                                         \
                static_cast<int>(_logFile.size()), _logFile.data(),            \
                loc.line(), static_cast<int>(_logFunction.size()),             \
                _logFunction.data(), ##__VA_ARGS__);                           \
        }                                                                      \
    } while (0)

#define INTERNAL_LOG_IMPL(logLevel, logTag, logFormat, ...)                    \
    do {                                                                       \
        if constexpr (static_logging_for(logLevel, logTag)) {                  \
            if (LoggingService::loggingFor(logLevel, logTag)) {                \
                static constexpr LogSiteId _logSiteId =                        \
                    Totem::LoggerBackend::detail::logSiteId(                   \
                        logFormat, logTag, logLevel);                          \
                (void)LoggingService::logfSite(                                \
                    logLevel, logTag, _logSiteId, logFormat, ##__VA_ARGS__);   \
            }                                                                  \
        }                                                                      \
    } while (0)

#define INTERNAL_LOG_RUNTIME_IMPL(logLevel, logTag, logFormat, ...)            \
    do {                                                                       \
        if (static_logging_for(logLevel, logTag) &&                            \
            LoggingService::loggingFor(logLevel, logTag)) {                    \
            (void)LoggingService::logf(logLevel, logTag, logFormat,            \
                                       ##__VA_ARGS__);                         \
        }                                                                      \
    } while (0)

#define _log_v(format, ...)                                                    \
    INTERNAL_LOG_IMPL(LogLevel::Verbose, logComponent, format, ##__VA_ARGS__)
#define _log_d(format, ...)                                                    \
    INTERNAL_LOG_IMPL(LogLevel::Debug, logComponent, format, ##__VA_ARGS__)
#define _log_i(format, ...)                                                    \
    INTERNAL_LOG_IMPL(LogLevel::Info, logComponent, format, ##__VA_ARGS__)
#define _log_w(format, ...)                                                    \
    INTERNAL_LOG_IMPL(LogLevel::Warning, logComponent, format, ##__VA_ARGS__)
#define _log_e(format, ...)                                                    \
    INTERNAL_LOG_IMPL(LogLevel::Error, logComponent, format, ##__VA_ARGS__)

#define _log(level, format, ...)                                               \
    INTERNAL_LOG_RUNTIME_IMPL(level, logComponent, format, ##__VA_ARGS__)
