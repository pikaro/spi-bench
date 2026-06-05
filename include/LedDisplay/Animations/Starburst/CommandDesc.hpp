#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/Starburst/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleStarburstCommand(CommandDesc::ParsedArgs args,
                                         void * /*unused*/) {
    auto config = StarburstConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, StarburstCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(rise, detail::optionalU32(args, 3, config.rise),
                           "Invalid animation rise argument");
    FAIL_IF_UNEXPECTED_FWD(peak, detail::optionalU32(args, 4, config.peak),
                           "Invalid animation peak argument");
    FAIL_IF_UNEXPECTED_FWD(wake, detail::optionalU32(args, 5, config.wake),
                           "Invalid animation wake argument");
    FAIL_IF_UNEXPECTED_FWD(points, detail::optionalU32(args, 6, config.points),
                           "Invalid animation point count argument");
    FAIL_IF_UNEXPECTED_FWD(pointGain,
                           detail::optionalU32(args, 7, config.pointGain),
                           "Invalid animation point gain argument");
    FAIL_IF_UNEXPECTED_FWD(twist, detail::optionalU32(args, 8, config.twist),
                           "Invalid animation twist argument");
    FAIL_IF_UNEXPECTED_FWD(cycles, detail::optionalU32(args, 9, config.cycles),
                           "Invalid animation cycle count argument");

    config.hue = detail::clampU8(hue);
    config.value = detail::clampU8(value);
    config.rise = detail::clampU8(rise);
    config.peak = std::max<uint8_t>(detail::clampU8(peak),
                                    StarburstCommand::minimumPeakRings);
    config.wake = detail::clampU8(wake);
    config.points = std::max<uint8_t>(detail::clampU8(points),
                                      StarburstCommand::minimumPoints);
    config.pointGain = detail::clampU8(pointGain);
    config.twist = detail::clampU8(twist);
    config.cycles = std::max<uint8_t>(detail::clampU8(cycles),
                                      StarburstCommand::minimumCycles);

    return detail::publishCommand(
        StarburstCommand::makeCommand(config, 0, detail::clampU16(duration)));
}

inline constexpr CommandDesc starburstSubcommand = {
    .name = "starburst",
    .description = "Publish a pointed center-burst animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "rise", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "peak", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "wake", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "points", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "pointGain", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "twist", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "cycles", CommandDesc::ArgRequirement::Optional)},
    .handler = handleStarburstCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
