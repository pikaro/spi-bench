#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/Shutter/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleShutterCommand(CommandDesc::ParsedArgs args,
                                       void * /*unused*/) {
    auto config = ShutterConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, ShutterCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(segments,
                           detail::optionalU32(args, 3, config.segments),
                           "Invalid animation segment argument");
    FAIL_IF_UNEXPECTED_FWD(openPct,
                           detail::optionalU32(args, 4, config.openPct),
                           "Invalid animation open argument");
    FAIL_IF_UNEXPECTED_FWD(edgeWidth,
                           detail::optionalU32(args, 5, config.edgeWidth),
                           "Invalid animation edge width argument");
    FAIL_IF_UNEXPECTED_FWD(rotationCycles,
                           detail::optionalU32(args, 6, config.rotationCycles),
                           "Invalid animation rotation cycle argument");
    FAIL_IF_UNEXPECTED_FWD(mode, detail::optionalU32(args, 7, config.mode),
                           "Invalid animation mode argument");

    config.hue = detail::clampU8(hue);
    config.value = detail::clampU8(value);
    config.segments = std::max<uint8_t>(detail::clampU8(segments),
                                        ShutterCommand::minimumSegments);
    config.openPct = detail::clampU8(openPct);
    config.edgeWidth = std::max<uint8_t>(detail::clampU8(edgeWidth),
                                         ShutterCommand::minimumEdgeWidth);
    config.rotationCycles = detail::clampU8(rotationCycles);
    config.mode = detail::clampU8(mode);

    return detail::publishCommand(
        ShutterCommand::makeCommand(config, 0, detail::clampU16(duration)));
}

inline constexpr CommandDesc shutterSubcommand = {
    .name = "shutter",
    .description = "Publish a folded aperture blade animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "segments", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "openPct", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "edgeWidth", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "rotationCycles", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "mode", CommandDesc::ArgRequirement::Optional)},
    .handler = handleShutterCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
