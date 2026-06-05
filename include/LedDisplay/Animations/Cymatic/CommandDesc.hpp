#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/Cymatic/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleCymaticCommand(CommandDesc::ParsedArgs args,
                                       void * /*unused*/) {
    auto config = CymaticConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, CymaticCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(sourceMode,
                           detail::optionalU32(args, 3, config.sourceMode),
                           "Invalid animation source mode argument");
    FAIL_IF_UNEXPECTED_FWD(wavelength,
                           detail::optionalU32(args, 4, config.wavelength),
                           "Invalid animation wavelength argument");
    FAIL_IF_UNEXPECTED_FWD(speed, detail::optionalU32(args, 5, config.speed),
                           "Invalid animation speed argument");
    FAIL_IF_UNEXPECTED_FWD(contrast,
                           detail::optionalU32(args, 6, config.contrast),
                           "Invalid animation contrast argument");
    FAIL_IF_UNEXPECTED_FWD(hueStep,
                           detail::optionalU32(args, 7, config.hueStep),
                           "Invalid animation hue step argument");

    config.hue = detail::clampU8(hue);
    config.value = detail::clampU8(value);
    config.sourceMode = detail::clampU8(sourceMode);
    config.wavelength = std::max<uint8_t>(detail::clampU8(wavelength),
                                          CymaticCommand::minimumWavelength);
    config.speed = detail::clampU8(speed);
    config.contrast = detail::clampU8(contrast);
    config.hueStep = detail::clampU8(hueStep);

    return detail::publishCommand(
        CymaticCommand::makeCommand(config, 0, detail::clampU16(duration)));
}

inline constexpr CommandDesc cymaticSubcommand = {
    .name = "cymatic",
    .description = "Publish a wave-interference field animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "sourceMode", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "wavelength", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "speed", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "contrast", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hueStep", CommandDesc::ArgRequirement::Optional)},
    .handler = handleCymaticCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
