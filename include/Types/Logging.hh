#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

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

constexpr const char *logLevelToString(LogLevel level) {
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

struct LogRecord {
    uint32_t ts;
    std::array<char, logTagLength> tag;
    LogLevel level;
    std::array<char, logMaxLength> msg;
};
