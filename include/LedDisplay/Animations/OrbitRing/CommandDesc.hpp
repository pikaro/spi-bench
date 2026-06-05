#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/OrbitRing/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleOrbitRingCommand(CommandDesc::ParsedArgs args,
                                         void * /*unused*/) {
    auto config = OrbitRingConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, OrbitRingCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(radius, detail::optionalU32(args, 3, config.radius),
                           "Invalid animation radius argument");
    FAIL_IF_UNEXPECTED_FWD(radialWidth,
                           detail::optionalU32(args, 4, config.radialWidth),
                           "Invalid animation radial width argument");
    FAIL_IF_UNEXPECTED_FWD(angularWidth,
                           detail::optionalU32(args, 5, config.angularWidth),
                           "Invalid animation angular width argument");
    FAIL_IF_UNEXPECTED_FWD(comets, detail::optionalU32(args, 6, config.comets),
                           "Invalid animation comet count argument");
    FAIL_IF_UNEXPECTED_FWD(laps, detail::optionalU32(args, 7, config.laps),
                           "Invalid animation lap count argument");
    FAIL_IF_UNEXPECTED_FWD(trail, detail::optionalU32(args, 8, config.trail),
                           "Invalid animation trail argument");
    FAIL_IF_UNEXPECTED_FWD(sparkle,
                           detail::optionalU32(args, 9, config.sparkle),
                           "Invalid animation sparkle argument");
    FAIL_IF_UNEXPECTED_FWD(hueJitter,
                           detail::optionalU32(args, 10, config.hueJitter),
                           "Invalid animation hue jitter argument");
    FAIL_IF_UNEXPECTED_FWD(radialDrift,
                           detail::optionalU32(args, 11, config.radialDrift),
                           "Invalid animation radial drift argument");
    FAIL_IF_UNEXPECTED_FWD(
        radialDirection, detail::optionalU32(args, 12, config.radialDirection),
        "Invalid animation radial direction argument");

    config.hue = detail::clampU8(hue);
    config.value = detail::clampU8(value);
    config.radius = detail::clampU8(radius);
    config.radialWidth = std::max<uint8_t>(detail::clampU8(radialWidth),
                                           OrbitRingCommand::minimumWidth);
    config.angularWidth = std::max<uint8_t>(detail::clampU8(angularWidth),
                                            OrbitRingCommand::minimumWidth);
    config.comets = std::max<uint8_t>(detail::clampU8(comets),
                                      OrbitRingCommand::minimumComets);
    config.laps =
        std::max<uint8_t>(detail::clampU8(laps), OrbitRingCommand::minimumLaps);
    config.trail = detail::clampU8(trail);
    config.sparkle = detail::clampU8(sparkle);
    config.hueJitter = detail::clampU8(hueJitter);
    config.radialDrift = detail::clampU8(radialDrift);
    config.radialDirection = std::min<uint8_t>(detail::clampU8(radialDirection),
                                               OrbitRingCommand::radialInward);

    return detail::publishCommand(
        OrbitRingCommand::makeCommand(config, 0, detail::clampU16(duration)));
}

inline constexpr CommandDesc orbitRingSubcommand = {
    .name = "orbit",
    .description = "Publish an orbiting radial-band lobe animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "radius", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "radialWidth", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "angularWidth", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "comets", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "laps", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "trail", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "sparkle", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hueJitter", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "radialDrift", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "radialDirection", CommandDesc::ArgRequirement::Optional)},
    .handler = handleOrbitRingCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
