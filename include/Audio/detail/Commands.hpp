#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstdint>
#include <span>

namespace Totem::Audio::detail {

template <typename Owner> struct Commands {
    static constexpr const char *beatKindName(BeatEventKind kind) {
        switch (kind) {
        case BeatEventKind::ExpectedHit:
            return "expected-hit";
        case BeatEventKind::ExpectedMiss:
            return "expected-miss";
        case BeatEventKind::Reacquired:
            return "reacquired";
        case BeatEventKind::Lost:
            return "lost";
        default:
            return "unknown";
        }
    }

    static constexpr const char *groupName(PeakGroup group) {
        switch (group) {
        case PeakGroup::Bass:
            return "bass";
        case PeakGroup::Mid:
            return "mid";
        case PeakGroup::High:
            return "high";
        default:
            return "unknown";
        }
    }

    static void logGroupStatus(PeakGroup group, const PeakGroupStatus &status,
                               bool indicator) {
        const auto rateWhole = status.ratePerMinuteHundredths / 100U;
        const auto rateFraction = status.ratePerMinuteHundredths % 100U;
        if (!status.hasPeak) {
            _log_i("Peak detector %s%s: rate=%lu.%02lu/min, energy=%u, "
                   "peaks=%lu, last=never",
                   groupName(group), indicator ? " indicator" : "",
                   static_cast<unsigned long>(rateWhole),
                   static_cast<unsigned long>(rateFraction), status.energy,
                   static_cast<unsigned long>(status.peaks));
            return;
        }

        _log_i("Peak detector %s%s: rate=%lu.%02lu/min, energy=%u, peaks=%lu, "
               "last=%lums ago",
               groupName(group), indicator ? " indicator" : "",
               static_cast<unsigned long>(rateWhole),
               static_cast<unsigned long>(rateFraction), status.energy,
               static_cast<unsigned long>(status.peaks),
               static_cast<unsigned long>(status.lastPeakAgeMs));
    }

    static ReturnCode handle_peaks(CommandDesc::ParsedArgs /*unused*/,
                                   void *ctx) {
        auto *analyzer = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(analyzer, ERR(CoreError, InvalidArgument),
                     "Audio peaks command context is null");

        const auto status = analyzer->peakStatus();
        for (size_t i = 0; i < peakGroupCount; ++i) {
            const auto group = static_cast<PeakGroup>(i);
            logGroupStatus(group, status.groups[i],
                           group == status.indicatorGroup);
        }
        return OK();
    }

    static ReturnCode handle_tempo(CommandDesc::ParsedArgs /*unused*/,
                                   void *ctx) {
        auto *analyzer = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(analyzer, ERR(CoreError, InvalidArgument),
                     "Audio tempo command context is null");

        const auto status = analyzer->tempoStatus();
        _log_i("Tempo tracker %s: bpm=%u, confidence=%u, last=%s, beats=%lu, "
               "hits=%lu, misses=%lu, reacquired=%lu, lost=%lu, age=%lums",
               status.locked ? "locked" : "unlocked", status.bpm,
               status.confidence, beatKindName(status.lastKind),
               static_cast<unsigned long>(status.beats),
               static_cast<unsigned long>(status.hits),
               static_cast<unsigned long>(status.misses),
               static_cast<unsigned long>(status.reacquired),
               static_cast<unsigned long>(status.lost),
               static_cast<unsigned long>(status.lastEventAgeMs));
        return OK();
    }

    static inline CommandDesc peaksCmd = {
        .needsContext = true,
        .name = "peaks",
        .description = "Output current audio peak detector status",
        .args = {},
        .handler = handle_peaks,
        .subcommands = {},
    };

    static inline CommandDesc tempoCmd = {
        .needsContext = true,
        .name = "tempo",
        .description = "Output current audio tempo tracker status",
        .args = {},
        .handler = handle_tempo,
        .subcommands = {},
    };

    static constexpr std::span<CommandDesc *> commands() {
        static auto commands = std::to_array<CommandDesc *>({
            &peaksCmd,
            &tempoCmd,
        });
        return commands;
    }
};

} // namespace Totem::Audio::detail
