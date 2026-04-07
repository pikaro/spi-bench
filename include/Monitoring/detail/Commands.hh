#pragma once

#include "Macros/Facade.hh"
#include "Monitoring/detail/Types.hh"
#include "StaticConfig/TaskController.hh"
#include "Support/Basic.hh"
#include "Support/Commands.hh"
#include "TaskController/Facade.hh"
#include "Types/Command.hh"
#include "Types/Error.hh"
#include <cstddef>

namespace Totem::Monitoring::detail {

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
        _log_i("    %2zu: " SV_FMT " (%8zu | %8zu) / %8zu "
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
        _log_i("    %2zu: " SV_FMT " <%s" SV_FMT SV_FMT "> p%3u @ c%2d "
               "(%5zuB / %5zuB = %6.2f%%) %6.2f%% in %8zums | %6.2f%% total",
               i, SV_ARG(task.name, TaskControllerConfig::maxTaskNameLen),
               task.hasEverStarted ? "S" : "X",
               SV_ARG(TaskController::state_to_string(task.state)),
               SV_ARG(TaskController::platform_state_to_string(
                   task.platformState)),
               task.currentPriority, task.coreId,
               stackSize - task.stackLowestFree, stackSize,
               (double)task.stackUsedPct, (double)task.runTimeDeltaPct,
               task.timestampDelta, (double)task.runTimeTotalPct);
    }

    return OK(CoreError);
}

inline static ReturnCode cmd_handle_monitoring(CommandDesc::Tokens /*unused*/,
                                               void *ctx);

inline static CommandDesc monitorCmd = {
    .needsContext = true,
    .name = "monitor",
    .description = "Output monitoring data and stats",
    .args = {},
    .minArgs = 0,
    .handler = cmd_handle_monitoring,
    .subcommands = {},
};

inline static ReturnCode register_commands(void *ctx) {
    auto &reg = Commands::registrar();

    FAIL_IF_ERR_FWD(reg.registerCommand(monitorCmd, ctx),
                    "Failed to register monitor command");

    return OK(CoreError);
}

} // namespace Totem::Monitoring::detail
