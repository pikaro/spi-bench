#pragma once

#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/FftReactive/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleFftReactiveCommand(CommandDesc::ParsedArgs args,
                                           void * /*unused*/) {
    auto config = FftReactiveConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, FftReactiveCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.baseHue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value,
                           detail::optionalU32(args, 2, config.valueScale),
                           "Invalid animation value argument");

    config.baseHue = detail::clampU8(hue);
    config.valueScale = detail::clampU8(value);

    return detail::publishCommand(
        FftReactiveCommand::makeCommand(config, 0, detail::clampU16(duration)));
}

inline constexpr CommandDesc fftReactiveSubcommand = {
    .name = "fft",
    .description = "Publish the persistent FFT reactive animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "valueScale", CommandDesc::ArgRequirement::Optional)},
    .handler = handleFftReactiveCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
