#pragma once

#include "Macros/internal/Error.hh"
#include "Types/Error.hh"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <string_view>

inline constexpr size_t logMaxLength = 256;
inline constexpr size_t logTagLength = 4;
static constexpr size_t logLevelLength = 3;

enum class LogLevel : uint8_t {
    Verbose = 0,
    Debug,
    Info,
    Warning,
    Error,
};

constexpr const char *log_level_to_string(LogLevel level) {
    switch (level) {
    case LogLevel::Verbose:
        return "VRB";
    case LogLevel::Debug:
        return "DBG";
    case LogLevel::Info:
        return "INF";
    case LogLevel::Warning:
        return "WRN";
    case LogLevel::Error:
        return "ERR";
    default:
        return "UN?";
    }
}

constexpr std::expected<LogLevel, ReturnCode>
log_level_from_string(const char *str) {
    if (str == nullptr) {
        return std::unexpected(ERR(CoreError, InvalidArgument));
    }
    if (strcmp(str, "verbose") == 0) {
        return LogLevel::Verbose;
    }
    if (strcmp(str, "debug") == 0) {
        return LogLevel::Debug;
    }
    if (strcmp(str, "info") == 0) {
        return LogLevel::Info;
    }
    if (strcmp(str, "warning") == 0) {
        return LogLevel::Warning;
    }
    if (strcmp(str, "error") == 0) {
        return LogLevel::Error;
    }
    return std::unexpected(ERR(CoreError, NotFound));
}

constexpr std::expected<LogLevel, ReturnCode>
log_level_from_string(std::string_view str) {
    if (str == "verbose") {
        return LogLevel::Verbose;
    }
    if (str == "debug") {
        return LogLevel::Debug;
    }
    if (str == "info") {
        return LogLevel::Info;
    }
    if (str == "warning") {
        return LogLevel::Warning;
    }
    if (str == "error") {
        return LogLevel::Error;
    }
    return std::unexpected(ERR(CoreError, NotFound));
}

struct LogRecord {
    uint32_t ts;
    std::array<char, logTagLength> tag;
    LogLevel level;
    std::array<char, logMaxLength> msg;
};
