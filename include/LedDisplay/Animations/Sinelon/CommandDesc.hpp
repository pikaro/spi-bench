#pragma once

#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/Sinelon/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleSinelonCommand(CommandDesc::ParsedArgs args,
                                       void * /*unused*/) {
    auto config = SinelonConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, SinelonCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(width, detail::optionalU32(args, 3, config.width),
                           "Invalid animation width argument");
    FAIL_IF_UNEXPECTED_FWD(period,
                           detail::optionalU32(args, 4, config.periodMs),
                           "Invalid animation period argument");
    FAIL_IF_UNEXPECTED_FWD(outerOrigin,
                           detail::optionalU32(args, 5, config.outerOrigin),
                           "Invalid animation origin argument");
    FAIL_IF_UNEXPECTED_FWD(travelRings,
                           detail::optionalU32(args, 6, config.travelRings),
                           "Invalid animation travel argument");
    FAIL_IF_UNEXPECTED_FWD(
        attenuation, detail::optionalU32(args, 7, config.bounceAttenuation),
        "Invalid animation attenuation argument");
    FAIL_IF_UNEXPECTED_FWD(spokeGain,
                           detail::optionalU32(args, 8, config.spokeGainPct),
                           "Invalid animation spoke gain argument");
    FAIL_IF_UNEXPECTED_FWD(
        spokeGainPhaseStep,
        detail::optionalU32(args, 9, config.spokeGainPhaseStep),
        "Invalid animation spoke gain phase argument");

    config.hue = detail::clampU8(hue);
    config.value = detail::clampU8(value);
    config.width =
        std::max<uint8_t>(detail::clampU8(width), SinelonCommand::minimumWidth);
    config.periodMs = std::max<uint16_t>(detail::clampU16(period),
                                         SinelonCommand::minimumPeriodMs);
    config.outerOrigin = outerOrigin != 0;
    config.travelRings = detail::clampU8(travelRings);
    config.bounceAttenuation = detail::clampU8(attenuation);
    config.spokeGainPct = detail::clampU16(spokeGain);
    config.spokeGainPhaseStep = detail::clampU8(spokeGainPhaseStep);

    return detail::publishCommand(
        SinelonCommand::makeCommand(config, 0, detail::clampU16(duration)));
}

inline constexpr CommandDesc sinelonSubcommand = {
    .name = "sinelon",
    .description = "Publish a persistent sine-head animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "width", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "periodMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "outerOrigin", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "travelRings", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "bounceAttenuation", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "spokeGainPct", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "spokeGainPhaseStep", CommandDesc::ArgRequirement::Optional)},
    .handler = handleSinelonCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
