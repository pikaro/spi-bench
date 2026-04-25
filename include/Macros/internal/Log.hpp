#pragma once

#include "Platform/PlatformSelect.hpp" // IWYU pragma: export
#include "Services/Logging.hpp"        // IWYU pragma: export
#include "LoggingBackend/Interfaces/Types.hpp"           // IWYU pragma: export
#include <array>                       // IWYU pragma: export
#include <source_location>             // IWYU pragma: export
#include <string_view>                 // IWYU pragma: export

namespace Totem::LoggerBackend::detail {

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
        auto loc = std::source_location::current();                            \
        auto _logFile =                                                        \
            Totem::LoggerBackend::detail::logFileName(loc.file_name());        \
        auto _logFunction = Totem::LoggerBackend::detail::logFunctionName(     \
            loc.function_name());                                              \
        (void)LoggingService::logf(                                            \
            LogLevel::Error, logComponent, "[%.*s:%d:%.*s] " msg,              \
            static_cast<int>(_logFile.size()), _logFile.data(), loc.line(),    \
            static_cast<int>(_logFunction.size()), _logFunction.data(),        \
            ##__VA_ARGS__);                                                    \
    } while (0)

#define INTERNAL_LOG_IMPL(logLevel, logTag, logFormat, ...)                    \
    do {                                                                       \
        if (LoggingService::loggingFor(logLevel, logTag)) {                    \
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
    INTERNAL_LOG_IMPL(level, logComponent, format, ##__VA_ARGS__)
