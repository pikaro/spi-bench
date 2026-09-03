#pragma once

#include "Types/Basic.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

inline constexpr size_t logMaxLength = 255;
inline constexpr size_t logTagLength = 3;
static constexpr size_t logLevelLength = 3;
using LogSiteId = uint32_t;
inline constexpr LogSiteId logUnknownSiteId = 0;

enum class LogLevel : uint8_t {
    Verbose = 0,
    Debug,
    Info,
    Warning,
    Error,
    Off,
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
    case LogLevel::Off:
        return "OFF";
    default:
        return "???";
    }
}

enum class LogComponent : uint8_t {
    Unknown = 0,
    System,
    Monitoring,
    Metrics,
    PubSub,
    Command,
    TaskControllerRegistry,
    Output,
    Rs485,
    Spi,
    Clock,
    LedPwm,
    StatusLed,
    Input,
    Esp,
    Audio,
};

constexpr const char *log_component_to_string(LogComponent component) {
    switch (component) {
    case LogComponent::System:
        return "sys";
    case LogComponent::Monitoring:
        return "mon";
    case LogComponent::Metrics:
        return "met";
    case LogComponent::PubSub:
        return "psb";
    case LogComponent::Command:
        return "cmd";
    case LogComponent::TaskControllerRegistry:
        return "tcr";
    case LogComponent::Output:
        return "out";
    case LogComponent::Rs485:
        return "rs4";
    case LogComponent::Spi:
        return "spi";
    case LogComponent::Clock:
        return "clk";
    case LogComponent::LedPwm:
        return "led";
    case LogComponent::StatusLed:
        return "sta";
    case LogComponent::Input:
        return "inp";
    case LogComponent::Esp:
        return "esp";
    case LogComponent::Audio:
        return "aud";
    case LogComponent::Unknown:
    default:
        return "???";
    }
}

constexpr Color log_component_to_color(LogComponent component) {
    switch (component) {
    case LogComponent::System:
    case LogComponent::Output:
    case LogComponent::Clock:
        return Color::Blue;

    case LogComponent::Monitoring:
    case LogComponent::Metrics:
        return Color::Cyan;

    case LogComponent::PubSub:
        return Color::Green;

    case LogComponent::Command:
        return Color::Yellow;

    case LogComponent::TaskControllerRegistry:
        return Color::Magenta;

    case LogComponent::Rs485:
    case LogComponent::Spi:
        return Color::Red;

    case LogComponent::LedPwm:
    case LogComponent::StatusLed:
        return Color::BrightWhite;

    case LogComponent::Input:
        return Color::BrightBlue;

    case LogComponent::Esp:
        return Color::BrightRed;

    case LogComponent::Audio:
        return Color::BrightGreen;

    case LogComponent::Unknown:
    default:
        return Color::White;
    }
}

inline constexpr LogComponent logComponent = LogComponent::System;

struct LogRecord {
    uint32_t ts;
    uint32_t tsSynced;
    LogSiteId siteId = logUnknownSiteId;
    LogComponent component;
    LogLevel level;
    std::array<char, logMaxLength> msg;
};
