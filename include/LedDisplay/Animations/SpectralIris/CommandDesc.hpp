#pragma once

#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/SpectralIris/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleSpectralIrisCommand(CommandDesc::ParsedArgs args,
                                            void * /*unused*/) {
    auto config = SpectralIrisConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, SpectralIrisCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.baseHue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(baseValue,
                           detail::optionalU32(args, 3, config.baseValue),
                           "Invalid animation base value argument");
    FAIL_IF_UNEXPECTED_FWD(petals, detail::optionalU32(args, 4, config.petals),
                           "Invalid animation petals argument");
    FAIL_IF_UNEXPECTED_FWD(aperture,
                           detail::optionalU32(args, 5, config.aperture),
                           "Invalid animation aperture argument");
    FAIL_IF_UNEXPECTED_FWD(rimWidth,
                           detail::optionalU32(args, 6, config.rimWidth),
                           "Invalid animation rim width argument");
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
    auto layer = SpectralIrisCommand::defaultLayer;
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
    config.petals = detail::clampU8(petals);
    config.aperture = detail::clampU8(aperture);
    config.rimWidth = detail::clampU8(rimWidth);
    config.contrast = detail::clampU8(contrast);
    config.peakSensitivity = detail::clampU8(peakSensitivity);
    config.flowSpeed = detail::clampU8(flowSpeed);
    config.hueModulation = detail::clampU8(hueModulation);

    return detail::publishCommand(SpectralIrisCommand::makeCommand(
        config, 0, detail::clampU16(duration), layer));
}

inline constexpr CommandDesc spectralIrisSubcommand = {
    .name = "iris",
    .description = "Publish the spectral iris FFT animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "baseValue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "petals", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "aperture", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "rimWidth", CommandDesc::ArgRequirement::Optional),
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
    .handler = handleSpectralIrisCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
