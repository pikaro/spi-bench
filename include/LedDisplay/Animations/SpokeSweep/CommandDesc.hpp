#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/SpokeSweep/Command.hpp"
#include "LedDisplay/Animations/SpokeSweep/Config.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleSpokeSweepCommand(CommandDesc::ParsedArgs args,
                                          void * /*unused*/) {
    auto config = SpokeSweepConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, SpokeSweepCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(baseHue,
                           detail::optionalU32(args, 1, config.baseHue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(trailSpokes,
                           detail::optionalU32(args, 3, config.trailSpokes),
                           "Invalid animation trail argument");
    FAIL_IF_UNEXPECTED_FWD(cycles, detail::optionalU32(args, 4, config.cycles),
                           "Invalid animation cycle argument");
    FAIL_IF_UNEXPECTED_FWD(markerValue,
                           detail::optionalU32(args, 5, config.markerValue),
                           "Invalid animation marker argument");

    config.baseHue = detail::clampU8(baseHue);
    config.value = detail::clampU8(value);
    config.trailSpokes = detail::clampU8(trailSpokes);
    config.cycles = detail::clampU8(cycles);
    config.markerValue = detail::clampU8(markerValue);

    return detail::publishCommand(SpokeSweepCommand::makeCommand(
        config, SpokeSweepCommand::defaultRequestId,
        detail::clampU16(duration)));
}

inline constexpr CommandDesc spokeSweepSubcommand = {
    .name = "sweep",
    .description = "Publish the spoke sweep diagnostic animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "baseHue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "trailSpokes", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "cycles", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "markerValue", CommandDesc::ArgRequirement::Optional)},
    .handler = handleSpokeSweepCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
