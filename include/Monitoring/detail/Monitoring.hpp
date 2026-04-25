#pragma once

#include "Base/HasCommands.hpp"
#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "Monitoring/Interfaces/IFrameSink.hpp"
#include "Monitoring/detail/Commands.hpp"
#include "Monitoring/detail/PlatformSelect.hpp"
#include "StaticConfig/TaskRegistry.hpp"
#include "TaskController/Interfaces/TaskRuntimeSnapshot.hpp"
#include "TaskControllerRegistry/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Totem::Monitoring::detail {

class Monitoring : public HasLifecycle<Monitoring>,
                   public HasCommands<Monitoring, Commands<Monitoring>> {
    friend class HasLifecycle<Monitoring>;
    friend struct LifecycleContract<Monitoring>;

  public:
    explicit Monitoring(TaskControllerRegistry::Registry &taskRegistry)
        : _taskRegistry(taskRegistry) {}
    DELETE_COPY(Monitoring)
    DELETE_MOVE(Monitoring)

    static constexpr const char *name = "Monitoring";

    ReturnCode snapshot(IFrameSink &sink) {
        auto now = ::platform::get_time();
        if (now < _lastSnapshotTimestamp) {
            // Time went backwards, likely due to overflow. Reset all totals to
            // avoid issues with negative deltas and such.
            _log_w("Time went backwards since last monitoring snapshot");
            _coreIdleTimeTotalMs.fill(0);
            _lastCoreIdleTimeTotalMs.fill(0);
            _coreUtilizationPctTotal.fill(0);
            _coreUtilizationPctDelta.fill(0.0F);
        }

        FAIL_IF_ERR_FWD(_populateMemoryStats(),
                        "Failed to populate memory stats");
        FAIL_IF_ERR_FWD(_populateTaskStats(),
                        "Failed to populate task monitoring stats");
        FAIL_IF_ERR_FWD(_populateCoreIdleTimes(),
                        "Failed to populate core idle times");
        FAIL_IF_ERR_FWD(_populateCoreUtilization(now),
                        "Failed to populate core utilization");

        MonitoringFrame frame;
        frame.timestamp = now;
        frame.global.memoryStats = _memoryStats;
        frame.global.coreIdleTimeTotalMs = _coreIdleTimeTotalMs;
        frame.global.coreIdleTimeDeltaMs = _coreIdleTimeDeltaMs;
        frame.global.coreUtilizationPctTotal = _coreUtilizationPctTotal;
        frame.global.coreUtilizationPctDelta = _coreUtilizationPctDelta;
        frame.global.taskCount = _taskCount;
        frame.tasks = std::span<const TaskController::TaskRuntimeSnapshot>(
            _taskSnapshots.data(), _taskCount);

        _lastSnapshotTimestamp = now;

        FAIL_IF_ERR_FWD(sink.consume(frame),
                        "Failed to consume monitoring frame in sink");

        return OK();
    }

  private:
    ReturnCode _onBegin() { return _registerCommands(); }
    ReturnCode _onEnd() { return _deregisterCommands(); }

    ReturnCode _populateMemoryStats() {
        return Platform::collect_memory_stats_into(_memoryStats);
    }

    ReturnCode _populateCoreIdleTimes() {
        return Platform::collect_cpu_free_into(_coreIdleTimeTotalMs);
    }

    ReturnCode _populateCoreUtilization(uint32_t timestampNow) {
        auto timestampDelta = timestampNow - _lastSnapshotTimestamp;

        for (size_t i = 0; i < ::platform::CoreCount; i++) {
            auto idleTimeTotal = _coreIdleTimeTotalMs[i];
            auto idleTimeDelta = idleTimeTotal - _lastCoreIdleTimeTotalMs[i];
            _coreIdleTimeDeltaMs[i] = idleTimeDelta;
            _lastCoreIdleTimeTotalMs[i] = idleTimeTotal;

            // Utilization is the percentage of time spent not idle since the
            // last snapshot, so 1 - (idle delta / total delta)
            auto utilizationPctDelta = 0.0F;
            if (timestampDelta > 0) {
                utilizationPctDelta =
                    100.0F * (1.0F - (static_cast<float>(idleTimeDelta) /
                                      static_cast<float>(timestampDelta)));
            }
            _coreUtilizationPctDelta[i] = utilizationPctDelta;

            // Total utilization is the percentage of time spent not idle since
            // the first snapshot, so 1 - (total idle / total time)
            auto utilizationPctTotal = 0.0F;
            if (timestampNow > 0) {
                utilizationPctTotal =
                    100.0F * (1.0F - (static_cast<float>(idleTimeTotal) /
                                      static_cast<float>(timestampNow)));
            }
            _coreUtilizationPctTotal[i] = utilizationPctTotal;
        }
        return OK();
    }

    ReturnCode _populateTaskStats() {
        _taskCount = 0;
        return _taskRegistry.forEachTaskSnapshot(
            [this](const TaskController::TaskRuntimeSnapshot &snapshot) {
                FAIL_IF(_taskCount >= _taskSnapshots.size(), ERR(OutOfMemory),
                        "Not enough space for all task snapshots");
                _taskSnapshots[_taskCount++] = snapshot;
                return OK();
            });
    }

    TaskControllerRegistry::Registry &_taskRegistry;
    uint32_t _lastSnapshotTimestamp = 0;
    std::array<MemoryStats, Platform::MemoryStatCount> _memoryStats{};
    std::array<uint32_t, ::platform::CoreCount> _coreIdleTimeTotalMs{0};
    std::array<uint32_t, ::platform::CoreCount> _lastCoreIdleTimeTotalMs{0};
    std::array<uint32_t, ::platform::CoreCount> _coreIdleTimeDeltaMs{0};
    std::array<float, ::platform::CoreCount> _coreUtilizationPctTotal{0.0F};
    std::array<float, ::platform::CoreCount> _coreUtilizationPctDelta{0.0F};
    static constexpr size_t MaxTaskSnapshots =
        static_cast<size_t>(TaskRegistryConfig::observedTaskCountMax);
    std::array<TaskController::TaskRuntimeSnapshot, MaxTaskSnapshots>
        _taskSnapshots{};
    uint8_t _taskCount = 0;
};

inline constexpr LifecycleContract<Monitoring> _monitoring_lifecycle;
inline constexpr CommandsContract<Monitoring, Commands<Monitoring>>
    _monitoring_commands_contract;

} // namespace Totem::Monitoring::detail
