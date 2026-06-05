#pragma once

#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/SpectralWeave/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleSpectralWeaveCommand(CommandDesc::ParsedArgs args,
                                             void * /*unused*/) {
    auto config = SpectralWeaveConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, SpectralWeaveCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.baseHue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(baseValue,
                           detail::optionalU32(args, 3, config.baseValue),
                           "Invalid animation base value argument");
    FAIL_IF_UNEXPECTED_FWD(radialMode,
                           detail::optionalU32(args, 4, config.radialMode),
                           "Invalid animation radial mode argument");
    FAIL_IF_UNEXPECTED_FWD(angularMode,
                           detail::optionalU32(args, 5, config.angularMode),
                           "Invalid animation angular mode argument");
    FAIL_IF_UNEXPECTED_FWD(symmetry,
                           detail::optionalU32(args, 6, config.symmetry),
                           "Invalid animation symmetry argument");
    FAIL_IF_UNEXPECTED_FWD(contrast,
                           detail::optionalU32(args, 7, config.contrast),
                           "Invalid animation contrast argument");
    FAIL_IF_UNEXPECTED_FWD(peakSensitivity,
                           detail::optionalU32(args, 8, config.peakSensitivity),
                           "Invalid animation peak sensitivity argument");
    FAIL_IF_UNEXPECTED_FWD(flowSpeed,
                           detail::optionalU32(args, 9, config.flowSpeed),
                           "Invalid animation flow speed argument");
    FAIL_IF_UNEXPECTED_FWD(hueModulation,
                           detail::optionalU32(args, 10, config.hueModulation),
                           "Invalid animation hue modulation argument");
    auto layer = SpectralWeaveCommand::defaultLayer;
    if (!detail::isOptionalDefaultMarker(args, 11)) {
        auto parsedLayer = args.get<Layer>(11);
        if (parsedLayer.ok) {
            layer = parsedLayer.value;
        } else if (parsedLayer.error != CommandDesc::ArgError::Missing) {
            FAIL(ERR(CoreError, InvalidArgument),
                 "Invalid animation layer argument");
        }
    }

    config.baseHue = detail::clampU8(hue);
    config.value = detail::clampU8(value);
    config.baseValue = detail::clampU8(baseValue);
    config.radialMode = detail::clampU8(radialMode);
    config.angularMode = detail::clampU8(angularMode);
    config.symmetry = detail::clampU8(symmetry);
    config.contrast = detail::clampU8(contrast);
    config.peakSensitivity = detail::clampU8(peakSensitivity);
    config.flowSpeed = detail::clampU8(flowSpeed);
    config.hueModulation = detail::clampU8(hueModulation);

    return detail::publishCommand(SpectralWeaveCommand::makeCommand(
        config, 0, detail::clampU16(duration), layer));
}

inline constexpr CommandDesc spectralWeaveSubcommand = {
    .name = "weave",
    .description = "Publish the persistent spectral weave animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "baseValue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "radialMode", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "angularMode", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "symmetry", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "contrast", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "peakSensitivity", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "flowSpeed", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hueModulation", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<Layer>(
                 "layer", CommandDesc::ArgRequirement::Optional)},
    .handler = handleSpectralWeaveCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
