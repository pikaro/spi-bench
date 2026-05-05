#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstdint>
#include <span>

namespace Totem::Audio::detail {

template <typename Owner> struct Commands {
    static constexpr const char *groupName(BeatGroup group) {
        switch (group) {
        case BeatGroup::Bass:
            return "bass";
        case BeatGroup::Mid:
            return "mid";
        case BeatGroup::High:
            return "high";
        default:
            return "unknown";
        }
    }

    static void logGroupStatus(BeatGroup group, const BeatGroupStatus &status,
                               bool primary) {
        const auto bpmWhole = status.bpmHundredths / 100U;
        const auto bpmFraction = status.bpmHundredths % 100U;
        if (!status.hasBeat) {
            _log_i("Beat tracker %s%s: bpm=%lu.%02lu, energy=%u, "
                   "beats=%lu, last=never",
                   groupName(group), primary ? " primary" : "",
                   static_cast<unsigned long>(bpmWhole),
                   static_cast<unsigned long>(bpmFraction), status.energy,
                   static_cast<unsigned long>(status.beats));
            return;
        }

        _log_i("Beat tracker %s%s: bpm=%lu.%02lu, energy=%u, beats=%lu, "
               "last=%lums ago",
               groupName(group), primary ? " primary" : "",
               static_cast<unsigned long>(bpmWhole),
               static_cast<unsigned long>(bpmFraction), status.energy,
               static_cast<unsigned long>(status.beats),
               static_cast<unsigned long>(status.lastBeatAgeMs));
    }

    static ReturnCode handle_bpm(CommandDesc::ParsedArgs /*unused*/,
                                 void *ctx) {
        auto *analyzer = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(analyzer, ERR(CoreError, InvalidArgument),
                     "Audio BPM command context is null");

        const auto status = analyzer->beatStatus();
        for (size_t i = 0; i < beatGroupCount; ++i) {
            const auto group = static_cast<BeatGroup>(i);
            logGroupStatus(group, status.groups[i],
                           group == status.primaryGroup);
        }
        return OK();
    }

    static inline CommandDesc bpmCmd = {
        .needsContext = true,
        .name = "bpm",
        .description = "Output current audio beat tracker BPM",
        .args = {},
        .handler = handle_bpm,
        .subcommands = {},
    };

    static constexpr std::span<CommandDesc *> commands() {
        static auto commands = std::to_array<CommandDesc *>({
            &bpmCmd,
        });
        return commands;
    }
};

} // namespace Totem::Audio::detail
