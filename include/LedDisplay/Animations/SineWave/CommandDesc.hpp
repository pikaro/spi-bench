#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/SineWave/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleSineWaveCommand(CommandDesc::ParsedArgs args,
                                        void * /*unused*/) {
    auto config = SineWaveConfig{};
    FAIL_IF_UNEXPECTED_FWD(duration,
                           detail::optionalU32(args, 0, config.durationMs),
                           "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(baseValue,
                           detail::optionalU32(args, 3, config.baseValue),
                           "Invalid animation base value argument");
    FAIL_IF_UNEXPECTED_FWD(width, detail::optionalU32(args, 4, config.width),
                           "Invalid animation width argument");
    FAIL_IF_UNEXPECTED_FWD(wavelength,
                           detail::optionalU32(args, 5, config.wavelength),
                           "Invalid animation wavelength argument");
    FAIL_IF_UNEXPECTED_FWD(outerOrigin,
                           detail::optionalU32(args, 6, config.outerOrigin),
                           "Invalid animation origin argument");
    FAIL_IF_UNEXPECTED_FWD(travelRings,
                           detail::optionalU32(args, 7, config.travelRings),
                           "Invalid animation travel argument");
    FAIL_IF_UNEXPECTED_FWD(spokeGain,
                           detail::optionalU32(args, 8, config.spokeGainPct),
                           "Invalid animation spoke gain argument");
    FAIL_IF_UNEXPECTED_FWD(tailDecay,
                           detail::optionalU32(args, 9, config.tailDecay),
                           "Invalid animation tail decay argument");
    FAIL_IF_UNEXPECTED_FWD(peakHold,
                           detail::optionalU32(args, 10, config.peakHold),
                           "Invalid animation peak hold argument");
    FAIL_IF_UNEXPECTED_FWD(lifetime, detail::optionalU32(args, 11, 0),
                           "Invalid animation lifetime argument");

    config.hue = detail::clampU8(hue);
    config.value = detail::clampU8(value);
    config.baseValue = detail::clampU8(baseValue);
    config.width = std::max<uint8_t>(detail::clampU8(width),
                                     SineWaveCommand::minimumWidth);
    config.durationMs = std::max<uint16_t>(detail::clampU16(duration),
                                           SineWaveCommand::minimumDurationMs);
    config.wavelength = std::max<uint8_t>(detail::clampU8(wavelength),
                                          SineWaveCommand::minimumWavelength);
    config.outerOrigin = outerOrigin != 0;
    config.travelRings = detail::clampU8(travelRings);
    config.spokeGainPct = detail::clampU16(spokeGain);
    config.tailDecay = detail::clampU8(tailDecay);
    config.peakHold = detail::clampU8(peakHold);

    return detail::publishCommand(
        SineWaveCommand::makeCommand(config, 0, detail::clampU16(lifetime)));
}

inline constexpr CommandDesc sineWaveSubcommand = {
    .name = "sine",
    .description = "Publish a traced sine-wave animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "baseValue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "width", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "wavelength", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "outerOrigin", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "travelRings", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "spokeGainPct", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "tailDecay", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "peakHold", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "lifetimeMs", CommandDesc::ArgRequirement::Optional)},
    .handler = handleSineWaveCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
