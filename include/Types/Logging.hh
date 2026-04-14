#pragma once

#include "Types/Basic.hh"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

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
        return "???";
    }
}

enum class LogComponent : uint8_t {
    System = 0,
    PubSub,
};

constexpr const char *log_component_to_string(LogComponent component) {
    switch (component) {
    case LogComponent::System:
        return "sys";
    case LogComponent::PubSub:
        return "psb";
    default:
        return "???";
    }
}

constexpr Color log_component_to_color(LogComponent component) {
    switch (component) {
    case LogComponent::System:
        return Color::Blue;
    case LogComponent::PubSub:
        return Color::Green;
    default:
        return Color::White;
    }
}

inline constexpr LogComponent logComponent = LogComponent::System;

struct LogRecord {
    uint32_t ts;
    LogComponent component;
    LogLevel level;
    std::array<char, logMaxLength> msg;
};
