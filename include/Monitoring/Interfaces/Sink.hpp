#pragma once

#include "TaskController/Interfaces/TaskRuntimeSnapshot.hh"
#include "Types/Error.hh"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Totem::Monitoring {

struct MemoryStats {
    std::string_view name;
    size_t totalBytes;
    size_t freeBytes;
    size_t minFreeBytes;
    float freePct;
    float minFreePct;
    uint32_t attrs;
    uint32_t flags;
};

struct GlobalMonitoringSnapshot {
    uint8_t taskCount = 0;
    std::span<uint32_t> coreIdleTimeTotalMs;
    std::span<uint32_t> coreIdleTimeDeltaMs;
    std::span<float> coreUtilizationPctTotal;
    std::span<float> coreUtilizationPctDelta;
    std::span<MemoryStats> memoryStats;
};

struct MonitoringFrame {
    uint32_t timestamp = 0;
    GlobalMonitoringSnapshot global;
    std::span<const TaskController::TaskRuntimeSnapshot> tasks;
};

struct Sink {
    void *self = nullptr;

    ReturnCode (*consumeHook)(void *, const MonitoringFrame &) = nullptr;

    ReturnCode consume(const MonitoringFrame &frame) const {
        return consumeHook(self, frame);
    }

    [[nodiscard]] bool validate() const {
        return self != nullptr && consumeHook != nullptr;
    }
};

} // namespace Totem::Monitoring
