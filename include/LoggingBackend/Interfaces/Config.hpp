#pragma once

#include "Platform/PlatformSelect.hpp"
#include "StaticConfig/Logging.hpp"
#include "StaticConfig/Stacks.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::LoggingBackend {

enum class RingBufferAllocation : uint8_t {
    Static,
    Dynamic,
};

struct AggregatorConfig {
    static constexpr size_t maxRingBufferSize =
        LoggingConfig::aggregatorRingBufferRecords;
    static constexpr bool hasStaticRingBufferStorage =
        LoggingConfig::aggregatorRingBufferStatic;

    size_t ringBufferSize = maxRingBufferSize;
    ::platform::Tick sendTimeoutMs = 0;
    ::platform::Tick receiveTimeoutMs = 5;
    RingBufferAllocation ringBufferAllocation =
        hasStaticRingBufferStorage ? RingBufferAllocation::Static
                                   : RingBufferAllocation::Dynamic;

    [[nodiscard]] bool validate() const {
        if (ringBufferSize == 0) {
            return false;
        }
        if (ringBufferAllocation == RingBufferAllocation::Dynamic) {
            return true;
        }
        return hasStaticRingBufferStorage && ringBufferSize <= maxRingBufferSize;
    }

    Totem::TaskController::Config task{
        .name = "Output",
        .stackSize = StaticConfig::TaskStacks::loggingAggregator,
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
        .stackSize = StaticConfig::TaskStacks::loggingErrorJournal,
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
