#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "Macros/Facade.hpp"
#include "Monitoring/Interfaces/IFrameSink.hpp"
#include "Monitoring/detail/Types.hpp"
#include "StaticConfig/TaskController.hpp"
#include "StaticConfig/TaskRegistry.hpp"
#include "Support/Basic.hpp"
#include "TaskController/Interfaces/TaskRuntimeSnapshot.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <span>

namespace Totem::Monitoring::detail {

inline const char *
task_allocation_str(TaskController::TaskAllocation allocation) {
    switch (allocation) {
    case TaskController::TaskAllocation::Static:
        return "T";
    case TaskController::TaskAllocation::Dynamic:
        return "D";
    }
    return "?";
}

static constexpr const char *task_state_str(TaskController::State state) {
    switch (state) {
    case TaskController::State::Stopped:
        return "X";
    case TaskController::State::Starting:
        return "B";
    case TaskController::State::Running:
        return "R";
    case TaskController::State::Stopping:
        return "H";
    default:
        return "?";
    }
}

static constexpr const char *
platform_state_str(TaskController::PlatformState state) {
    switch (state) {
    case TaskController::PlatformState::Running:
        return "R";
    case TaskController::PlatformState::Ready:
        return "W";
    case TaskController::PlatformState::Blocked:
        return "B";
    case TaskController::PlatformState::Suspended:
        return "S";
    default:
        return "?";
    }
}

inline static ReturnCode dump_monitoring_snaphot(const MonitoringFrame &frame) {
    _log_i("Monitoring snapshot at timestamp %lu:", frame.timestamp);

    _log_i("  Core utilization:");
    for (size_t core = 0; core < frame.global.coreUtilizationPctTotal.size();
         ++core) {
        _log_i("    Core %zu: Total %5.2f%%, Delta %5.2f%%", core,
               (double)frame.global.coreUtilizationPctTotal[core],
               (double)frame.global.coreUtilizationPctDelta[core]);
    }

    _log_i("  Memory stats (%zu pools):", frame.global.memoryStats.size());
    for (size_t i = 0; i < frame.global.memoryStats.size(); ++i) {
        const auto &stat = frame.global.memoryStats[i];
        _log_i("    %2zu: " SV_FMT " (%8zu free | %8zu min) / %8zu "
               "= (%6.2f%% | %6.2f%%) "
               "[%1s%1s%1s%1s%1s%1s%1s] "
               "[%1s%1s%1s]",
               i, SV_ARG(stat.name, 16), stat.freeBytes, stat.minFreeBytes,
               stat.totalBytes, (double)stat.freePct, (double)stat.minFreePct,
               (has_flag(stat.attrs, MemoryAttr::Internal)) ? "I" : "",
               (has_flag(stat.attrs, MemoryAttr::External)) ? "E" : "",
               (has_flag(stat.attrs, MemoryAttr::GeneralPurpose)) ? "G" : "",
               (has_flag(stat.attrs, MemoryAttr::DefaultAlloc)) ? "A" : "",
               (has_flag(stat.attrs, MemoryAttr::DmaCapable)) ? "D" : "",
               (has_flag(stat.attrs, MemoryAttr::Retained)) ? "R" : "",
               (has_flag(stat.attrs, MemoryAttr::FastRtc)) ? "F" : "",
               (has_flag(stat.flags, MemoryStatFlags::Overlapping)) ? "O" : "",
               (has_flag(stat.flags, MemoryStatFlags::Conditional)) ? "C" : "",
               (has_flag(stat.flags, MemoryStatFlags::Specialized)) ? "S" : ""

        );
    }

    _log_i("  Tasks (%u):", frame.global.taskCount);
    if (frame.tasks.empty()) {
        _log_i("    None");
        return OK(CoreError);
    }

    for (size_t i = 0; i < frame.tasks.size(); ++i) {
        const auto &task = frame.tasks[i];
        const size_t stackSize =
            task.config != nullptr ? task.config->stackSize : 0;
        if (stackSize > 0 && task.stackLowestFree <= stackSize) {
            _log_i(
                "    %2zu: " SV_FMT " -> " SV_FMT " <%s%s%s> p%3u @ c%2d "
                "[%s] (%5zuB / %5zuB = %6.2f%%) %6.2f%% in %8zums | "
                "%6.2f%% total",
                i,
                SV_ARG(task.sourceName, TaskRegistryConfig::sourceNameMaxLen),
                SV_ARG(task.name, -TaskControllerConfig::maxTaskNameLen),
                task.hasEverStarted ? "S" : "X", task_state_str(task.state),
                platform_state_str(task.platformState), task.currentPriority,
                task.coreId, task_allocation_str(task.allocation),
                stackSize - task.stackLowestFree, stackSize,
                (double)task.stackUsedPct, (double)task.runTimeDeltaPct,
                task.timestampDelta, (double)task.runTimeTotalPct);
            continue;
        }

        _log_i("    %2zu: " SV_FMT " -> " SV_FMT " <%s%s%s> p%3u @ c%2d "
               "[%s] ( low %5luB              ) %6.2f%% in %8zums | "
               "%6.2f%% total",
               i, SV_ARG(task.sourceName, TaskRegistryConfig::sourceNameMaxLen),
               SV_ARG(task.name, -TaskControllerConfig::maxTaskNameLen),
               task.hasEverStarted ? "S" : "X", task_state_str(task.state),
               platform_state_str(task.platformState), task.currentPriority,
               task.coreId, task_allocation_str(task.allocation),
               static_cast<unsigned long>(task.stackLowestFree),
               (double)task.runTimeDeltaPct, task.timestampDelta,
               (double)task.runTimeTotalPct);
    }

    return OK(CoreError);
}

template <typename Owner> struct Commands {
    static ReturnCode handle_monitoring(CommandDesc::ParsedArgs /*unused*/,
                                        void *ctx) {
        auto *monitoring = static_cast<Owner *>(ctx);
        struct DumpSink final : IFrameSink {
            ReturnCode consume(const MonitoringFrame &frame) override {
                return dump_monitoring_snaphot(frame);
            }
        } sink;

        return monitoring->snapshot(sink);
    }

    static inline CommandDesc monitorCmd = {
        .needsContext = true,
        .name = "monitor",
        .description = "Output monitoring data and stats",
        .args = {},
        .handler = handle_monitoring,
        .subcommands = {},
    };

    static constexpr std::span<CommandDesc *> commands() {
        static auto commands = std::to_array<CommandDesc *>({
            &monitorCmd,
        });
        return commands;
    }
};

} // namespace Totem::Monitoring::detail
