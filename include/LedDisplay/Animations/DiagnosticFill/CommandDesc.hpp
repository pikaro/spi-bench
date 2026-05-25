#pragma once

#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/DiagnosticFill/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleDiagnosticFillCommand(CommandDesc::ParsedArgs args,
                                              void * /*unused*/) {
    auto config = DiagnosticFillConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, DiagnosticFillCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");

    config.hue = detail::clampU8(hue);
    config.value = detail::clampU8(value);

    return detail::publishCommand(DiagnosticFillCommand::makeCommand(
        config, 0, detail::clampU16(duration)));
}

inline constexpr CommandDesc diagnosticFillSubcommand = {
    .name = "fill",
    .description = "Publish a diagnostic fill animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional)},
    .handler = handleDiagnosticFillCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
