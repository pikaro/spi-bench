#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include <cstddef>
#include <cstdint>

struct LoggingMinimum {
    static constexpr LogLevel defaultLevel = LogLevel::Info;
    static constexpr LogLevel system = defaultLevel;
    static constexpr LogLevel monitoring = defaultLevel;
    static constexpr LogLevel metrics = defaultLevel;
    static constexpr LogLevel pubSub = defaultLevel;
    static constexpr LogLevel command = defaultLevel;
    static constexpr LogLevel taskControllerRegistry = defaultLevel;
    static constexpr LogLevel output = defaultLevel;
    static constexpr LogLevel rs485 = defaultLevel;
    static constexpr LogLevel spi = defaultLevel;
    static constexpr LogLevel clock = defaultLevel;
};

static constexpr LogLevel logging_minimum_for(LogComponent component) {
    switch (component) {
    case LogComponent::System:
        return LoggingMinimum::system;
    case LogComponent::Monitoring:
        return LoggingMinimum::monitoring;
    case LogComponent::Metrics:
        return LoggingMinimum::metrics;
    case LogComponent::PubSub:
        return LoggingMinimum::pubSub;
    case LogComponent::Command:
        return LoggingMinimum::command;
    case LogComponent::TaskControllerRegistry:
        return LoggingMinimum::taskControllerRegistry;
    case LogComponent::Output:
        return LoggingMinimum::output;
    case LogComponent::Rs485:
        return LoggingMinimum::rs485;
    case LogComponent::Spi:
        return LoggingMinimum::spi;
    case LogComponent::Clock:
        return LoggingMinimum::clock;
    case LogComponent::Unknown:
    default:
        return LoggingMinimum::defaultLevel;
    }
}

static constexpr bool static_logging_for(LogLevel level,
                                         LogComponent component) {
    return static_cast<uint8_t>(level) >=
           static_cast<uint8_t>(logging_minimum_for(component));
}

struct Tracing {
    static constexpr LogLevel rs485 = LoggingMinimum::rs485;
    static constexpr LogLevel spi = LoggingMinimum::spi;
    static constexpr LogLevel pubSub = LoggingMinimum::pubSub;
};

static constexpr bool tracing_for(LogLevel componentMinimum) {
    return static_cast<uint8_t>(LogLevel::Verbose) >=
           static_cast<uint8_t>(componentMinimum);
}

struct LoggingConfig {
    static constexpr std::size_t maxSinks = 3;
    static constexpr bool useColor = true;
    static constexpr LogLevel defaultLogLevel = LogLevel::Debug;
};
