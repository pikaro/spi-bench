#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include <cstddef>
#include <optional>

struct LoggingMinimumLevel {
    static constexpr LogLevel defaultMinimum = LogLevel::Verbose;
    static constexpr LogLevel system = defaultMinimum;
    static constexpr LogLevel monitoring = defaultMinimum;
    static constexpr LogLevel metrics = defaultMinimum;
    static constexpr LogLevel pubSub = defaultMinimum;
    static constexpr LogLevel command = defaultMinimum;
    static constexpr LogLevel taskControllerRegistry = defaultMinimum;
    static constexpr LogLevel output = defaultMinimum;
    static constexpr LogLevel rs485 = defaultMinimum;
    static constexpr LogLevel spi = defaultMinimum;
    static constexpr LogLevel clock = defaultMinimum;
};

struct LoggingDefaultLevel {
    static constexpr LogLevel defaultLevel = LogLevel::Info;
    static constexpr std::optional<LogLevel> system = std::nullopt;
    static constexpr std::optional<LogLevel> monitoring = std::nullopt;
    static constexpr std::optional<LogLevel> metrics = std::nullopt;
    static constexpr std::optional<LogLevel> pubSub = std::nullopt;
    static constexpr std::optional<LogLevel> command = std::nullopt;
    static constexpr std::optional<LogLevel> taskControllerRegistry =
        std::nullopt;
    static constexpr std::optional<LogLevel> output = std::nullopt;
    static constexpr std::optional<LogLevel> rs485 = std::nullopt;
    static constexpr std::optional<LogLevel> spi = LogLevel::Verbose;
    static constexpr std::optional<LogLevel> clock = std::nullopt;
};

struct LoggingConfig {
    static constexpr std::size_t maxSinks = 3;
    static constexpr bool useColor = true;
};
