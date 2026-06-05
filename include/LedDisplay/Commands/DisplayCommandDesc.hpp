#pragma once

#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include "LedDisplay/Interfaces/AnimationCommandFactory.hpp"
#include <cstdint>

namespace Totem::LedDisplay::Commands {

inline ReturnCode handleHueOffsetCommand(CommandDesc::ParsedArgs args,
                                         void * /*unused*/) {
    auto parsed = args.get<uint32_t>(0);
    FAIL_IF_NOT(parsed.ok, ERR(CoreError, InvalidArgument),
                "Invalid hue offset argument");
    return Animations::detail::publishCommand(
        makeHueOffsetCommand(Animations::detail::rawAngleU8(parsed.value)));
}

inline ReturnCode handleRotationOffsetCommand(CommandDesc::ParsedArgs args,
                                              void * /*unused*/) {
    auto parsed = args.get<uint32_t>(0);
    FAIL_IF_NOT(parsed.ok, ERR(CoreError, InvalidArgument),
                "Invalid rotation offset argument");
    return Animations::detail::publishCommand(makeRotationOffsetCommand(
        Animations::detail::degreesAngleU8(parsed.value)));
}

inline ReturnCode handleBrightnessCommand(CommandDesc::ParsedArgs args,
                                          void * /*unused*/) {
    auto parsed = args.get<uint32_t>(0);
    FAIL_IF_NOT(parsed.ok, ERR(CoreError, InvalidArgument),
                "Invalid brightness argument");
    return Animations::detail::publishCommand(
        makeBrightnessCommand(Animations::detail::clampU8(parsed.value)));
}

inline constexpr CommandDesc hueOffsetSubcommand = {
    .name = "hue",
    .description = "Publish a global LED hue offset",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>("offset")},
    .handler = handleHueOffsetCommand,
    .subcommands = {},
};

inline constexpr CommandDesc rotationOffsetSubcommand = {
    .name = "rot",
    .description = "Publish a global LED rotation offset in degrees",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>("degrees")},
    .handler = handleRotationOffsetCommand,
    .subcommands = {},
};

inline constexpr CommandDesc brightnessSubcommand = {
    .name = "brightness",
    .description = "Publish a global LED brightness value",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>("value")},
    .handler = handleBrightnessCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Commands
