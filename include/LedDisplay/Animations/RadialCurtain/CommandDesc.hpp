#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/RadialCurtain/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleRadialCurtainCommand(CommandDesc::ParsedArgs args,
                                             void * /*unused*/) {
    auto config = RadialCurtainConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, RadialCurtainCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(width, detail::optionalU32(args, 3, config.width),
                           "Invalid animation width argument");
    FAIL_IF_UNEXPECTED_FWD(tilt, detail::optionalU32(args, 4, config.tilt),
                           "Invalid animation tilt argument");
    FAIL_IF_UNEXPECTED_FWD(speed, detail::optionalU32(args, 5, config.speed),
                           "Invalid animation speed argument");
    FAIL_IF_UNEXPECTED_FWD(outerOrigin,
                           detail::optionalU32(args, 6, config.outerOrigin),
                           "Invalid animation origin argument");
    FAIL_IF_UNEXPECTED_FWD(spokePhase,
                           detail::optionalU32(args, 7, config.spokePhase),
                           "Invalid animation spoke phase argument");

    config.hue = detail::clampU8(hue);
    config.value = detail::clampU8(value);
    config.width = std::max<uint8_t>(detail::clampU8(width),
                                     RadialCurtainCommand::minimumWidth);
    config.tilt = detail::clampU8(tilt);
    config.speed = std::max<uint8_t>(detail::clampU8(speed),
                                     RadialCurtainCommand::minimumSpeed);
    config.outerOrigin = outerOrigin != 0;
    config.spokePhase = detail::clampU8(spokePhase);

    return detail::publishCommand(RadialCurtainCommand::makeCommand(
        config, 0, detail::clampU16(duration)));
}

inline constexpr CommandDesc radialCurtainSubcommand = {
    .name = "curtain",
    .description = "Publish a slanted radial curtain animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "width", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "tilt", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "speed", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "outerOrigin", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "spokePhase", CommandDesc::ArgRequirement::Optional)},
    .handler = handleRadialCurtainCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
