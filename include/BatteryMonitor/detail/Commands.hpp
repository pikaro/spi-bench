// IWYU pragma: private

#pragma once

#include "CommandBackend/Facade.hpp"
#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cinttypes>
#include <magic_enum/magic_enum.hpp>
#include <span>

namespace Totem::BatteryMonitor::detail {

template <typename Owner> struct Commands {
    static ReturnCode handleStatus(CommandDesc::ParsedArgs /*unused*/,
                                   void *ctx) {
        auto *monitor = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(monitor, ERR(CoreError, InvalidArgument),
                     "Battery command context is null");
        auto statusResult = monitor->status();
        if (!statusResult) {
            return statusResult.error();
        }
        const auto &status = *statusResult;
        if (!status.latestMeasurement.has_value()) {
            _log_w(
                "Battery status: no measurement received; calibration=" SV_FMT
                ", profile=%s, storage=" SV_FMT,
                MAGIC_SV_ARG(status.calibrationState),
                status.profile.active ? "learned" : "nominal",
                MAGIC_SV_ARG(status.storageHealth));
            return OK();
        }

        const auto &measurement = *status.latestMeasurement;
        _log_i("Battery status: source=" SV_FMT ", voltage=%" PRIu32
               " mV, current=%" PRId32 " uA, power=%" PRId32 " mW, soc=%" PRIu32
               ".%01" PRIu32 "%%, remaining=%" PRIu32 " mAh/%" PRIu32
               " mWh, freshness=" SV_FMT,
               MAGIC_SV_ARG(status.sourceState), measurement.voltageMillivolts,
               measurement.currentMicroamps, measurement.powerMilliwatts,
               status.stateOfChargePartsPerThousand / 10U,
               status.stateOfChargePartsPerThousand % 10U,
               status.remainingMilliampHours, status.remainingMilliwattHours,
               MAGIC_SV_ARG(status.measurementFreshness));
        if (status.timeToEmptyMinutes.has_value()) {
            _log_i("Battery estimate: average=%" PRIu32
                   " mW, time-to-empty=%" PRIu32 " min, confidence=" SV_FMT,
                   status.averagePowerMilliwatts, *status.timeToEmptyMinutes,
                   MAGIC_SV_ARG(status.confidence));
        } else {
            _log_i("Battery estimate: average=%" PRIu32
                   " mW, time-to-empty=unavailable, confidence=" SV_FMT,
                   status.averagePowerMilliwatts,
                   MAGIC_SV_ARG(status.confidence));
        }
        _log_i("Battery calibration: state=" SV_FMT ", reason=" SV_FMT
               ", used=%" PRIu32 " mAh/%" PRIu32 " mWh, intervals=%" PRIu32
               ", max-gap=%" PRIu32 " ms, storage=" SV_FMT,
               MAGIC_SV_ARG(status.calibrationState),
               MAGIC_SV_ARG(status.calibrationReason),
               status.dischargedMilliampHours, status.dischargedMilliwattHours,
               status.persistedCalibrationIntervals, status.maximumSampleGapMs,
               MAGIC_SV_ARG(status.storageHealth));
        return OK();
    }

    static ReturnCode handleCalibrationStart(CommandDesc::ParsedArgs /*unused*/,
                                             void *ctx) {
        auto *monitor = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(monitor, ERR(CoreError, InvalidArgument),
                     "Battery command context is null");
        const auto result = monitor->requestCalibrationStart();
        if (!result.queued()) {
            auto status = monitor->status();
            if (result.reason != BatteryCalibrationInvalidReason::None &&
                status && status->latestMeasurement.has_value()) {
                const auto &measurement = *status->latestMeasurement;
                _log_w("Battery calibration start rejected: reason=" SV_FMT
                       ", state=" SV_FMT ", voltage=%" PRIu32
                       " mV, current=%" PRId32 " uA, freshness=" SV_FMT
                       ", storage=" SV_FMT,
                       MAGIC_SV_ARG(result.reason),
                       MAGIC_SV_ARG(status->calibrationState),
                       measurement.voltageMillivolts,
                       measurement.currentMicroamps,
                       MAGIC_SV_ARG(status->measurementFreshness),
                       MAGIC_SV_ARG(status->storageHealth));
            } else if (result.reason != BatteryCalibrationInvalidReason::None &&
                       status) {
                _log_w("Battery calibration start rejected: reason=" SV_FMT
                       ", state=" SV_FMT
                       ", measurement=unavailable, storage=" SV_FMT,
                       MAGIC_SV_ARG(result.reason),
                       MAGIC_SV_ARG(status->calibrationState),
                       MAGIC_SV_ARG(status->storageHealth));
            } else if (status) {
                _log_w("Battery calibration start rejected: error=" ERR_FMT
                       ", state=" SV_FMT ", storage=" SV_FMT,
                       ERR_ARG(result.error),
                       MAGIC_SV_ARG(status->calibrationState),
                       MAGIC_SV_ARG(status->storageHealth));
            } else {
                _log_w("Battery calibration start rejected: error=" ERR_FMT,
                       ERR_ARG(result.error));
            }
            return result.error;
        }
        _log_i("Battery calibration start queued for the owner task");
        return OK();
    }

    static ReturnCode handleCalibrationAbort(CommandDesc::ParsedArgs /*unused*/,
                                             void *ctx) {
        auto *monitor = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(monitor, ERR(CoreError, InvalidArgument),
                     "Battery command context is null");
        FAIL_IF_ERR_FWD(monitor->abortCalibration(),
                        "Failed to abort battery calibration");
        _log_w("Battery calibration abort queued for the owner task");
        return OK();
    }

    static ReturnCode handleCalibrationStatus(CommandDesc::ParsedArgs args,
                                              void *ctx) {
        return handleStatus(args, ctx);
    }

    static ReturnCode handleProfiles(CommandDesc::ParsedArgs /*unused*/,
                                     void *ctx) {
        auto *monitor = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(monitor, ERR(CoreError, InvalidArgument),
                     "Battery command context is null");
        auto status = monitor->status();
        if (!status) {
            return status.error();
        }
        const auto &journal = status->catalog;
        _log_i("Battery profiles: complete=%" PRIu32 ", incompatible=%" PRIu32
               ", incomplete=%" PRIu32 ", corrupt-records=%" PRIu32
               ", records=%" PRIu32 ", highest-session=%" PRIu32,
               journal.completeSessionCount, journal.incompatibleSessionCount,
               journal.incompleteSessionCount, journal.corruptRecordCount,
               journal.journalRecordCount, journal.highestSessionId);
        if (status->profile.active) {
            const auto &profile = status->profile;
            _log_i("Active battery profile: session=%" PRIu32
                   ", usable=%" PRIu32 " mAh/%" PRIu32 " mWh, duration=%" PRIu32
                   " s, cutoff=%" PRIu32 " mV, points=%u",
                   profile.sessionId, profile.usableMilliampHours,
                   profile.usableMilliwattHours, profile.durationSeconds,
                   profile.observedCutoffMillivolts,
                   static_cast<unsigned>(profile.pointCount));
        } else {
            _log_i("No compatible completed battery profile is active");
        }
        return OK();
    }

    static inline constinit std::array<CommandDesc, 3> calibrationSubcommands{{
        {
            .name = "start",
            .description = "Arm a full-pack discharge calibration",
            .args = {},
            .handler = handleCalibrationStart,
            .subcommands = {},
        },
        {
            .name = "abort",
            .description = "Abort the active discharge calibration",
            .args = {},
            .handler = handleCalibrationAbort,
            .subcommands = {},
        },
        {
            .name = "status",
            .description = "Print discharge calibration status",
            .args = {},
            .handler = handleCalibrationStatus,
            .subcommands = {},
        },
    }};

    static inline constinit std::array<CommandDesc, 3> subcommands{{
        {
            .name = "status",
            .description = "Print battery estimate and source status",
            .args = {},
            .handler = handleStatus,
            .subcommands = {},
        },
        {
            .name = "calibrate",
            .description = "Manage discharge calibration",
            .args = {},
            .handler = handleCalibrationStatus,
            .subcommands = calibrationSubcommands,
        },
        {
            .name = "profiles",
            .description = "List battery calibration profiles",
            .args = {},
            .handler = handleProfiles,
            .subcommands = {},
        },
    }};

    static inline constinit CommandDesc batteryCmd = {
        .needsContext = true,
        .name = "battery",
        .description = "Inspect battery state and calibration",
        .args = {},
        .handler = handleStatus,
        .subcommands = subcommands,
    };

    static constexpr std::span<CommandDesc *> commands() {
        static auto commands = std::to_array<CommandDesc *>({&batteryCmd});
        return commands;
    }
};

} // namespace Totem::BatteryMonitor::detail
