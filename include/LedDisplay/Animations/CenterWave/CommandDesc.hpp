#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/CenterWave/Command.hpp"
#include "LedDisplay/Animations/CenterWave/Config.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleCenterWaveCommand(CommandDesc::ParsedArgs args,
                                          void * /*unused*/) {
    auto config = CenterWaveConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, CenterWaveCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(rise, detail::optionalU32(args, 3, config.rise),
                           "Invalid animation rise argument");
    FAIL_IF_UNEXPECTED_FWD(peak, detail::optionalU32(args, 4, config.peak),
                           "Invalid animation peak argument");
    FAIL_IF_UNEXPECTED_FWD(wake, detail::optionalU32(args, 5, config.wake),
                           "Invalid animation wake argument");

    config.hue = detail::clampU8(hue);
    config.value = detail::clampU8(value);
    config.rise = detail::clampU8(rise);
    config.peak = std::max<uint8_t>(detail::clampU8(peak),
                                    CenterWaveCommand::minimumPeakRings);
    config.wake = detail::clampU8(wake);

    return detail::publishCommand(
        CenterWaveCommand::makeCommand(config, 0, detail::clampU16(duration)));
}

inline constexpr CommandDesc centerWaveSubcommand = {
    .name = "wave",
    .description = "Publish a center wave animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "rise", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "peak", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "wake", CommandDesc::ArgRequirement::Optional)},
    .handler = handleCenterWaveCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
