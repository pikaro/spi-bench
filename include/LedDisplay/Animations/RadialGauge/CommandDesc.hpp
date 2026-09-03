#pragma once

#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/RadialGauge/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleRadialGaugeCommand(CommandDesc::ParsedArgs args,
                                           void * /*unused*/) {
    auto config = RadialGaugeConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, RadialGaugeCommand::defaultLifetimeMs),
        "Invalid radial gauge duration argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 1, config.value),
                           "Invalid radial gauge value argument");
    FAIL_IF_UNEXPECTED_FWD(maximumValue,
                           detail::optionalU32(args, 2, config.maximumValue),
                           "Invalid radial gauge maximum argument");
    FAIL_IF_UNEXPECTED_FWD(startHue,
                           detail::optionalU32(args, 3, config.startHue),
                           "Invalid radial gauge start hue argument");
    FAIL_IF_UNEXPECTED_FWD(startSaturation,
                           detail::optionalU32(args, 4, config.startSaturation),
                           "Invalid radial gauge start saturation argument");
    FAIL_IF_UNEXPECTED_FWD(startValue,
                           detail::optionalU32(args, 5, config.startValue),
                           "Invalid radial gauge start value argument");
    FAIL_IF_UNEXPECTED_FWD(endHue, detail::optionalU32(args, 6, config.endHue),
                           "Invalid radial gauge end hue argument");
    FAIL_IF_UNEXPECTED_FWD(endSaturation,
                           detail::optionalU32(args, 7, config.endSaturation),
                           "Invalid radial gauge end saturation argument");
    FAIL_IF_UNEXPECTED_FWD(endValue,
                           detail::optionalU32(args, 8, config.endValue),
                           "Invalid radial gauge end value argument");
    FAIL_IF_UNEXPECTED_FWD(centerRing,
                           detail::optionalU32(args, 9, config.centerRing),
                           "Invalid radial gauge center ring argument");
    FAIL_IF_UNEXPECTED_FWD(ringWidth,
                           detail::optionalU32(args, 10, config.ringWidth),
                           "Invalid radial gauge ring width argument");

    config.value = detail::clampU16(value);
    config.maximumValue = detail::clampU16(maximumValue);
    config.startHue = detail::clampU8(startHue);
    config.startSaturation = detail::clampU8(startSaturation);
    config.startValue = detail::clampU8(startValue);
    config.endHue = detail::clampU8(endHue);
    config.endSaturation = detail::clampU8(endSaturation);
    config.endValue = detail::clampU8(endValue);
    config.centerRing = detail::clampU8(centerRing);
    config.ringWidth = std::max<uint8_t>(detail::clampU8(ringWidth), 1U);

    return detail::publishCommand(RadialGaugeCommand::makeCommand(
        config, RadialGaugeCommand::defaultRequestId,
        detail::clampU16(duration)));
}

inline constexpr CommandDesc radialGaugeSubcommand = {
    .name = "gauge",
    .description = "Publish a configurable radial gauge",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "maximumValue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "startHue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "startSaturation", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "startValue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "endHue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "endSaturation", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "endValue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "centerRing", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "ringWidth", CommandDesc::ArgRequirement::Optional)},
    .handler = handleRadialGaugeCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
