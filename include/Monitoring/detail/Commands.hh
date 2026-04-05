#pragma once

#include "Macros/Facade.hh"
#include "Monitoring/detail/Types.hh"
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
        _log_i("    Core %zu: Total %.2f%%, Delta %.2f%%", core,
               frame.global.coreUtilizationPctTotal[core],
               frame.global.coreUtilizationPctDelta[core]);
    }

    for (size_t i = 0; i < frame.global.memoryStats.size(); ++i) {
        const auto &stat = frame.global.memoryStats[i];
        _log_i("  Memory pool %zu: " SV_FMT, i, SV_ARG(stat.name));
        _log_i("    Total bytes: %zu", stat.totalBytes);
        _log_i("    Free bytes: %zu", stat.freeBytes);
        _log_i("    Min free bytes: %zu", stat.minFreeBytes);
        _log_i("    Free %%: %.2f%%", stat.freePct);
        _log_i("    Min free %%: %.2f%%", stat.minFreePct);
        _log_i("    Attrs: %s%s%s%s%s%s%s",
               (stat.attrs & to_bits(MemoryAttr::Internal)) ? "I" : "",
               (stat.attrs & to_bits(MemoryAttr::External)) ? "E" : "",
               (stat.attrs & to_bits(MemoryAttr::GeneralPurpose)) ? "G" : "",
               (stat.attrs & to_bits(MemoryAttr::DefaultAlloc)) ? "A" : "",
               (stat.attrs & to_bits(MemoryAttr::DmaCapable)) ? "D" : "",
               (stat.attrs & to_bits(MemoryAttr::Retained)) ? "R" : "",
               (stat.attrs & to_bits(MemoryAttr::FastRtc)) ? "F" : "");
        _log_i("    Flags: %s%s%s",
               (stat.flags & to_bits(MemoryStatFlags::Overlapping)) ? "O" : "",
               (stat.flags & to_bits(MemoryStatFlags::Conditional)) ? "C" : "",
               (stat.flags & to_bits(MemoryStatFlags::Specialized)) ? "S" : "");

        _log_i("Tasks:");
    }

    for (size_t i = 0; i < frame.tasks.size(); ++i) {
        const auto &task = frame.tasks[i];
        _log_i("  Task %zu: " SV_FMT, i, SV_ARG(task.name));
        _log_i("    Started: %s", task.hasEverStarted ? "Yes" : "No");
        _log_i("    State: " SV_FMT,
               SV_ARG(TaskController::state_to_string(task.state)));
        _log_i("    Platform state: " SV_FMT,
               SV_ARG(TaskController::platform_state_to_string(
                   task.platformState)));
        _log_i("    Current priority: %u", task.currentPriority);
        _log_i("    Total run time %%: %.2f%%", task.runTimeTotalPct);
        _log_i("    Delta run time %%: %.2f%%", task.runTimeDeltaPct);
        _log_i("    Stack used %%: %.2f%%", task.stackUsedPct);
        if (task.lastStopResult.has_value()) {
            _log_i("    Last stop result: " SV_FMT,
                   SV_ARG(TaskController::exit_reason_to_string(
                       task.lastStopResult->reason)));
        }

        return OK(CoreError);
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
