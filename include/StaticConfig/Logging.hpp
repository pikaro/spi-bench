#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include <cstddef>
#include <optional>

struct LoggingMinimumLevel {
    static constexpr LogLevel defaultMinimum = LogLevel::Info;
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
    static constexpr LogLevel ledPwm = defaultMinimum;
    static constexpr LogLevel statusLed = defaultMinimum;
    static constexpr LogLevel input = LogLevel::Verbose;
    static constexpr LogLevel esp = LogLevel::Verbose;
    static constexpr LogLevel audio = LogLevel::Verbose;
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
    static constexpr std::optional<LogLevel> ledPwm = std::nullopt;
    static constexpr std::optional<LogLevel> statusLed = std::nullopt;
    static constexpr std::optional<LogLevel> input = std::nullopt;
    static constexpr std::optional<LogLevel> esp = std::nullopt;
    static constexpr std::optional<LogLevel> audio = std::nullopt;
};

struct LoggingConfig {
    static constexpr bool useColor = true;

    static constexpr bool aggregatorRingBufferStatic = true;
    static constexpr std::size_t aggregatorRingBufferRecords = 100;

    static constexpr bool errorJournalEnabled = true;
    static constexpr std::size_t errorJournalHistoryRecords = 5;
    static constexpr std::size_t errorJournalTrailingRecords = 5;
    static constexpr std::size_t errorJournalSlots = 5;
    static constexpr std::size_t errorJournalSeenSites = 64;
    static constexpr std::size_t errorJournalQueueRecords = 12;
    static constexpr std::size_t errorJournalMessageLength = 96;

    static constexpr std::size_t maxSinks =
        1 + (errorJournalEnabled ? 1 : 0);
};
