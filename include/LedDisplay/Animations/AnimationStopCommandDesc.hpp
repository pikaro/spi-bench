#pragma once

#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include "LedDisplay/Interfaces/AnimationCommandFactory.hpp"
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleAnimationStopCommand(CommandDesc::ParsedArgs args,
                                             void * /*unused*/) {
    FAIL_IF_UNEXPECTED_FWD(requestId, detail::optionalU32(args, 0, 0),
                           "Invalid animation request ID argument");
    return detail::publishCommand(Totem::LedDisplay::makeStopAnimationCommand(
        detail::clampU16(requestId)));
}

inline constexpr CommandDesc animationStopSubcommand = {
    .name = "stop",
    .description = "Publish an animation stop command",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
        "requestId", CommandDesc::ArgRequirement::Optional)},
    .handler = handleAnimationStopCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
