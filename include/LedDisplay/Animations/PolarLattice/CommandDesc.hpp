#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/PolarLattice/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include "Types/Error.hpp"
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handlePolarLatticeCommand(CommandDesc::ParsedArgs args,
                                            void * /*unused*/) {
    auto config = PolarLatticeConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, PolarLatticeCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(radialMode,
                           detail::optionalU32(args, 3, config.radialMode),
                           "Invalid animation radial mode argument");
    FAIL_IF_UNEXPECTED_FWD(angularMode,
                           detail::optionalU32(args, 4, config.angularMode),
                           "Invalid animation angular mode argument");
    FAIL_IF_UNEXPECTED_FWD(speed, detail::optionalU32(args, 5, config.speed),
                           "Invalid animation speed argument");
    FAIL_IF_UNEXPECTED_FWD(mix, detail::optionalU32(args, 6, config.mix),
                           "Invalid animation mix argument");
    FAIL_IF_UNEXPECTED_FWD(contrast,
                           detail::optionalU32(args, 7, config.contrast),
                           "Invalid animation contrast argument");

    config.hue = detail::clampU8(hue);
    config.value = detail::clampU8(value);
    config.radialMode = detail::clampU8(radialMode);
    config.angularMode = detail::clampU8(angularMode);
    config.speed = detail::clampU8(speed);
    config.mix = detail::clampU8(mix);
    config.contrast = detail::clampU8(contrast);

    return detail::publishCommand(PolarLatticeCommand::makeCommand(
        config, 0, detail::clampU16(duration)));
}

inline constexpr CommandDesc polarLatticeSubcommand = {
    .name = "lattice",
    .description = "Publish a polar lattice field animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "radialMode", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "angularMode", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "speed", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "mix", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "contrast", CommandDesc::ArgRequirement::Optional)},
    .handler = handlePolarLatticeCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
