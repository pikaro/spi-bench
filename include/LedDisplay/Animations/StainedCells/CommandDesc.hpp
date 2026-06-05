#pragma once

#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/StainedCells/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleStainedCellsCommand(CommandDesc::ParsedArgs args,
                                            void * /*unused*/) {
    auto config = StainedCellsConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, StainedCellsCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.baseHue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(baseValue,
                           detail::optionalU32(args, 3, config.baseValue),
                           "Invalid animation base value argument");
    FAIL_IF_UNEXPECTED_FWD(seedCount,
                           detail::optionalU32(args, 4, config.seedCount),
                           "Invalid animation seed count argument");
    FAIL_IF_UNEXPECTED_FWD(borderWidth,
                           detail::optionalU32(args, 5, config.borderWidth),
                           "Invalid animation border width argument");
    FAIL_IF_UNEXPECTED_FWD(interiorValue,
                           detail::optionalU32(args, 6, config.interiorValue),
                           "Invalid animation interior value argument");
    FAIL_IF_UNEXPECTED_FWD(driftSpeed,
                           detail::optionalU32(args, 7, config.driftSpeed),
                           "Invalid animation drift speed argument");
    FAIL_IF_UNEXPECTED_FWD(contrast,
                           detail::optionalU32(args, 8, config.contrast),
                           "Invalid animation contrast argument");
    FAIL_IF_UNEXPECTED_FWD(peakSensitivity,
                           detail::optionalU32(args, 9, config.peakSensitivity),
                           "Invalid animation peak sensitivity argument");
    FAIL_IF_UNEXPECTED_FWD(seed, detail::optionalU32(args, 10, config.seed),
                           "Invalid animation seed argument");
    FAIL_IF_UNEXPECTED_FWD(hueModulation,
                           detail::optionalU32(args, 11, config.hueModulation),
                           "Invalid animation hue modulation argument");
    auto layer = StainedCellsCommand::defaultLayer;
    if (!detail::isOptionalDefaultMarker(args, 12)) {
        auto parsedLayer = args.get<Layer>(12);
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
    config.seedCount = detail::clampU8(seedCount);
    config.borderWidth = detail::clampU8(borderWidth);
    config.interiorValue = detail::clampU8(interiorValue);
    config.driftSpeed = detail::clampU8(driftSpeed);
    config.contrast = detail::clampU8(contrast);
    config.peakSensitivity = detail::clampU8(peakSensitivity);
    config.seed = detail::clampU8(seed);
    config.hueModulation = detail::clampU8(hueModulation);

    return detail::publishCommand(StainedCellsCommand::makeCommand(
        config, 0, detail::clampU16(duration), layer));
}

inline constexpr CommandDesc stainedCellsSubcommand = {
    .name = "cells",
    .description = "Publish the stained cells FFT animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "baseValue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "seedCount", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "borderWidth", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "interiorValue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "driftSpeed", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "contrast", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "peakSensitivity", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "seed", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hueModulation", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<Layer>(
                 "layer", CommandDesc::ArgRequirement::Optional)},
    .handler = handleStainedCellsCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
