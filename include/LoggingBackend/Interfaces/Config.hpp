#pragma once

#include "Platform/PlatformSelect.hpp"
#include "StaticConfig/Logging.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::LoggingBackend {

struct AggregatorConfig {
    size_t ringBufferSize = 100;
    ::platform::Tick sendTimeoutMs = 0;
    ::platform::Tick receiveTimeoutMs = 5;

    [[nodiscard]] bool validate() const { return ringBufferSize > 0; }

    Totem::TaskController::Config task{
        .name = "Output",
        .stackSize = 4096,
        .intervalMs = 10,
    };
};

struct ConsoleConfig {
    // Logging tolerates line delay, so prefer the driver's buffered TX path by
    // default and avoid forcing a full wire drain on every record.
    bool flush = false;

    [[nodiscard]] static bool validate() { return true; }
};

struct ErrorJournalConfig {
    static constexpr std::size_t historyRecords =
        LoggingConfig::errorJournalHistoryRecords;
    static constexpr std::size_t trailingRecords =
        LoggingConfig::errorJournalTrailingRecords;
    static constexpr std::size_t errorSlots =
        LoggingConfig::errorJournalSlots;
    static constexpr std::size_t seenSites =
        LoggingConfig::errorJournalSeenSites;
    static constexpr std::size_t queueRecords =
        LoggingConfig::errorJournalQueueRecords;
    static constexpr std::size_t journalMessageLength =
        LoggingConfig::errorJournalMessageLength;
    static constexpr std::size_t lineBufferSize = 256;

    const char *path = "/errors.log";
    std::size_t minFreeBytes = 4096;
    uint32_t hotTimeoutMs = 1000;

    Totem::TaskController::Config task{
        .name = "LogJournal",
        .priority = 1,
        .stackSize = 4096,
        .intervalMs = 1000,
        .noCatchup = true,
        .useNotify = true,
        .notifyExpectTimeout = true,
        .notifyTimeoutMs = 1000,
        .autoRestart = false,
    };

    [[nodiscard]] bool validate() const {
        return path != nullptr && path[0] == '/' && minFreeBytes > 0 &&
               hotTimeoutMs > 0 && task.validate();
    }
};

} // namespace Totem::LoggingBackend
