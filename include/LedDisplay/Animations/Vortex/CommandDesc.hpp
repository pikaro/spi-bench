#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/Vortex/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleVortexCommand(CommandDesc::ParsedArgs args,
                                      void * /*unused*/) {
    auto config = VortexConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, VortexCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(arms, detail::optionalU32(args, 3, config.arms),
                           "Invalid animation arms argument");
    FAIL_IF_UNEXPECTED_FWD(twist, detail::optionalU32(args, 4, config.twist),
                           "Invalid animation twist argument");
    FAIL_IF_UNEXPECTED_FWD(width, detail::optionalU32(args, 5, config.width),
                           "Invalid animation width argument");
    FAIL_IF_UNEXPECTED_FWD(cycles, detail::optionalU32(args, 6, config.cycles),
                           "Invalid animation cycle count argument");
    FAIL_IF_UNEXPECTED_FWD(hueStep,
                           detail::optionalU32(args, 7, config.hueStep),
                           "Invalid animation hue step argument");

    config.hue = detail::clampU8(hue);
    config.value = detail::clampU8(value);
    config.arms =
        std::max<uint8_t>(detail::clampU8(arms), VortexCommand::minimumArms);
    config.twist = detail::clampU8(twist);
    config.width =
        std::max<uint8_t>(detail::clampU8(width), VortexCommand::minimumWidth);
    config.cycles = std::max<uint8_t>(detail::clampU8(cycles),
                                      VortexCommand::minimumCycles);
    config.hueStep = detail::clampU8(hueStep);

    return detail::publishCommand(
        VortexCommand::makeCommand(config, 0, detail::clampU16(duration)));
}

inline constexpr CommandDesc vortexSubcommand = {
    .name = "vortex",
    .description = "Publish a rotating spiral field animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "arms", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "twist", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "width", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "cycles", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hueStep", CommandDesc::ArgRequirement::Optional)},
    .handler = handleVortexCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
