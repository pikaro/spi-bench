#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/Lighthouse/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleLighthouseCommand(CommandDesc::ParsedArgs args,
                                          void * /*unused*/) {
    auto config = LighthouseConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, LighthouseCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(beamWidth,
                           detail::optionalU32(args, 3, config.beamWidth),
                           "Invalid animation beam width argument");
    FAIL_IF_UNEXPECTED_FWD(trailSpokes,
                           detail::optionalU32(args, 4, config.trailSpokes),
                           "Invalid animation trail argument");
    FAIL_IF_UNEXPECTED_FWD(cycles, detail::optionalU32(args, 5, config.cycles),
                           "Invalid animation cycle count argument");
    FAIL_IF_UNEXPECTED_FWD(innerRing,
                           detail::optionalU32(args, 6, config.innerRing),
                           "Invalid animation inner ring argument");
    FAIL_IF_UNEXPECTED_FWD(outerRing,
                           detail::optionalU32(args, 7, config.outerRing),
                           "Invalid animation outer ring argument");

    config.hue = detail::clampU8(hue);
    config.value = detail::clampU8(value);
    config.beamWidth = std::max<uint8_t>(detail::clampU8(beamWidth),
                                         LighthouseCommand::minimumBeamWidth);
    config.trailSpokes = detail::clampU8(trailSpokes);
    config.cycles = std::max<uint8_t>(detail::clampU8(cycles),
                                      LighthouseCommand::minimumCycles);
    config.innerRing = detail::clampU8(innerRing);
    config.outerRing = detail::clampU8(outerRing);

    return detail::publishCommand(
        LighthouseCommand::makeCommand(config, 0, detail::clampU16(duration)));
}

inline constexpr CommandDesc lighthouseSubcommand = {
    .name = "lighthouse",
    .description = "Publish a rotating beam animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "beamWidth", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "trailSpokes", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "cycles", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "innerRing", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "outerRing", CommandDesc::ArgRequirement::Optional)},
    .handler = handleLighthouseCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
