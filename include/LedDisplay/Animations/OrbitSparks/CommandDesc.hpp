#pragma once

#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/OrbitSparks/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleOrbitSparksCommand(CommandDesc::ParsedArgs args,
                                           void * /*unused*/) {
    auto config = OrbitSparksConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, OrbitSparksCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.baseHue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(sparkCount,
                           detail::optionalU32(args, 3, config.sparkCount),
                           "Invalid animation spark count argument");
    FAIL_IF_UNEXPECTED_FWD(sparkSize,
                           detail::optionalU32(args, 4, config.sparkSize),
                           "Invalid animation spark size argument");
    FAIL_IF_UNEXPECTED_FWD(orbitSpeed,
                           detail::optionalU32(args, 5, config.orbitSpeed),
                           "Invalid animation orbit speed argument");
    FAIL_IF_UNEXPECTED_FWD(radialDrift,
                           detail::optionalU32(args, 6, config.radialDrift),
                           "Invalid animation radial drift argument");
    FAIL_IF_UNEXPECTED_FWD(highSparkle,
                           detail::optionalU32(args, 7, config.highSparkle),
                           "Invalid animation high sparkle argument");
    FAIL_IF_UNEXPECTED_FWD(peakSensitivity,
                           detail::optionalU32(args, 8, config.peakSensitivity),
                           "Invalid animation peak sensitivity argument");
    FAIL_IF_UNEXPECTED_FWD(seed, detail::optionalU32(args, 9, config.seed),
                           "Invalid animation seed argument");
    FAIL_IF_UNEXPECTED_FWD(hueModulation,
                           detail::optionalU32(args, 10, config.hueModulation),
                           "Invalid animation hue modulation argument");
    auto layer = OrbitSparksCommand::defaultLayer;
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
    config.sparkCount = detail::clampU8(sparkCount);
    config.sparkSize = detail::clampU8(sparkSize);
    config.orbitSpeed = detail::clampU8(orbitSpeed);
    config.radialDrift = detail::clampU8(radialDrift);
    config.highSparkle = detail::clampU8(highSparkle);
    config.peakSensitivity = detail::clampU8(peakSensitivity);
    config.seed = detail::clampU8(seed);
    config.hueModulation = detail::clampU8(hueModulation);

    return detail::publishCommand(OrbitSparksCommand::makeCommand(
        config, 0, detail::clampU16(duration), layer));
}

inline constexpr CommandDesc orbitSparksSubcommand = {
    .name = "sparks",
    .description = "Publish the orbit sparks FFT animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "sparkCount", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "sparkSize", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "orbitSpeed", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "radialDrift", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "highSparkle", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "peakSensitivity", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "seed", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hueModulation", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<Layer>(
                 "layer", CommandDesc::ArgRequirement::Optional)},
    .handler = handleOrbitSparksCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
