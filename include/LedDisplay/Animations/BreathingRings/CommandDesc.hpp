#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/BreathingRings/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleBreathingRingsCommand(CommandDesc::ParsedArgs args,
                                              void * /*unused*/) {
    auto config = BreathingRingsConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, BreathingRingsCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(spacing,
                           detail::optionalU32(args, 3, config.spacing),
                           "Invalid animation spacing argument");
    FAIL_IF_UNEXPECTED_FWD(width, detail::optionalU32(args, 4, config.width),
                           "Invalid animation width argument");
    FAIL_IF_UNEXPECTED_FWD(cycles, detail::optionalU32(args, 5, config.cycles),
                           "Invalid animation cycle count argument");
    FAIL_IF_UNEXPECTED_FWD(direction,
                           detail::optionalU32(args, 6, config.direction),
                           "Invalid animation direction argument");
    FAIL_IF_UNEXPECTED_FWD(hueStep,
                           detail::optionalU32(args, 7, config.hueStep),
                           "Invalid animation hue step argument");

    config.hue = detail::clampU8(hue);
    config.value = detail::clampU8(value);
    config.spacing = std::max<uint8_t>(detail::clampU8(spacing),
                                       BreathingRingsCommand::minimumSpacing);
    config.width = std::max<uint8_t>(detail::clampU8(width),
                                     BreathingRingsCommand::minimumWidth);
    config.cycles = std::max<uint8_t>(detail::clampU8(cycles),
                                      BreathingRingsCommand::minimumCycles);
    config.direction = detail::clampU8(direction);
    config.hueStep = detail::clampU8(hueStep);

    return detail::publishCommand(BreathingRingsCommand::makeCommand(
        config, 0, detail::clampU16(duration)));
}

inline constexpr CommandDesc breathingRingsSubcommand = {
    .name = "rings",
    .description = "Publish a breathing ring animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "spacing", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "width", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "cycles", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "direction", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hueStep", CommandDesc::ArgRequirement::Optional)},
    .handler = handleBreathingRingsCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
