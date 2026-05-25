#pragma once

#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/WheelIndicator/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace Totem::LedDisplay::Animations {

inline std::expected<WheelIndicatorConfig, ReturnCode>
parseWheelIndicatorConfig(CommandDesc::ParsedArgs args, size_t firstIndex) {
    auto config = WheelIndicatorConfig{};
    FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
        hue, detail::optionalU32(args, firstIndex, config.hue),
        "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
        value, detail::optionalU32(args, firstIndex + 1U, config.value),
        "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
        spokes, detail::optionalU32(args, firstIndex + 2U, config.spokes),
        "Invalid animation spokes argument");
    FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
        falloff, detail::optionalU32(args, firstIndex + 3U, config.falloff),
        "Invalid animation falloff argument");

    config.hue = detail::clampU8(hue);
    config.value = detail::clampU8(value);
    config.spokes = std::max<uint8_t>(detail::clampU8(spokes),
                                      WheelIndicatorCommand::minimumSpokes);
    config.falloff = detail::clampU8(falloff);
    return config;
}

inline ReturnCode handleWheelIndicatorCommand(CommandDesc::ParsedArgs args,
                                              void * /*unused*/) {
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, WheelIndicatorCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(config, parseWheelIndicatorConfig(args, 1),
                           "Invalid wheel indicator config arguments");
    FAIL_IF_UNEXPECTED_FWD(
        requestId,
        detail::optionalU32(args, 5, WheelIndicatorCommand::defaultRequestId),
        "Invalid wheel indicator request ID argument");

    return detail::publishCommand(WheelIndicatorCommand::makeCommand(
        config, detail::clampU16(requestId), detail::clampU16(duration)));
}

inline ReturnCode
handleWheelIndicatorUpdateCommand(CommandDesc::ParsedArgs args,
                                  void * /*unused*/) {
    FAIL_IF_UNEXPECTED_FWD(config, parseWheelIndicatorConfig(args, 0),
                           "Invalid wheel indicator update arguments");
    FAIL_IF_UNEXPECTED_FWD(
        requestId,
        detail::optionalU32(args, 4, WheelIndicatorCommand::defaultRequestId),
        "Invalid wheel indicator request ID argument");

    return detail::publishCommand(WheelIndicatorCommand::makeUpdateCommand(
        config, detail::clampU16(requestId)));
}

inline constexpr CommandDesc wheelIndicatorSubcommand = {
    .name = "wheel",
    .description = "Publish the persistent wheel indicator animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "spokes", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "falloff", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "requestId", CommandDesc::ArgRequirement::Optional)},
    .handler = handleWheelIndicatorCommand,
    .subcommands = {},
};

inline constexpr CommandDesc wheelIndicatorUpdateSubcommand = {
    .name = "wheel-update",
    .description = "Publish a wheel indicator update",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "spokes", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "falloff", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "requestId", CommandDesc::ArgRequirement::Optional)},
    .handler = handleWheelIndicatorUpdateCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
