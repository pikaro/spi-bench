#pragma once

#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include "LedDisplay/Interfaces/AnimationCommandFactory.hpp"
#include <cstdint>
#include <limits>

namespace Totem::LedDisplay::Commands {

inline ReturnCode handleLayerActiveCommand(CommandDesc::ParsedArgs args,
                                           void * /*unused*/) {
    auto parsedLayer = args.get<Layer>(0);
    FAIL_IF_NOT(parsedLayer.ok, ERR(CoreError, InvalidArgument),
                "Invalid layer argument");
    auto parsedActive = args.get<bool>(1);
    FAIL_IF_NOT(parsedActive.ok, ERR(CoreError, InvalidArgument),
                "Invalid layer active argument");
    return Animations::detail::publishCommand(
        makeLayerActiveCommand(parsedLayer.value, parsedActive.value));
}

inline ReturnCode handleLayerOpacityCommand(CommandDesc::ParsedArgs args,
                                            void * /*unused*/) {
    auto parsedLayer = args.get<Layer>(0);
    FAIL_IF_NOT(parsedLayer.ok, ERR(CoreError, InvalidArgument),
                "Invalid layer argument");
    auto parsedOpacity = args.get<uint32_t>(1);
    FAIL_IF_NOT(parsedOpacity.ok, ERR(CoreError, InvalidArgument),
                "Invalid layer opacity argument");
    return Animations::detail::publishCommand(makeLayerOpacityCommand(
        parsedLayer.value, Animations::detail::clampU8(parsedOpacity.value)));
}

inline ReturnCode handleLayerSwapCommand(CommandDesc::ParsedArgs args,
                                         void * /*unused*/) {
    auto parsedFirst = args.get<Layer>(0);
    FAIL_IF_NOT(parsedFirst.ok, ERR(CoreError, InvalidArgument),
                "Invalid first layer argument");
    auto parsedSecond = args.get<Layer>(1);
    FAIL_IF_NOT(parsedSecond.ok, ERR(CoreError, InvalidArgument),
                "Invalid second layer argument");
    auto parsedDuration = args.get<uint32_t>(2);
    FAIL_IF_NOT(parsedDuration.ok, ERR(CoreError, InvalidArgument),
                "Invalid layer swap duration argument");
    FAIL_IF(parsedDuration.value == 0 ||
                parsedDuration.value > std::numeric_limits<uint16_t>::max(),
            ERR(CoreError, InvalidArgument),
            "Layer swap duration must be between 1 and 65535ms");
    return Animations::detail::publishCommand(
        makeLayerFadeSwapCommand(parsedFirst.value, parsedSecond.value,
                                 static_cast<uint16_t>(parsedDuration.value)));
}

inline constexpr CommandDesc layerActiveSubcommand = {
    .name = "active",
    .description = "Publish a layer active state",
    .args = {Totem::CommandBackend::detail::arg<Layer>("layer"),
             Totem::CommandBackend::detail::arg<bool>("active")},
    .handler = handleLayerActiveCommand,
    .subcommands = {},
};

inline constexpr CommandDesc layerOpacitySubcommand = {
    .name = "opacity",
    .description = "Publish a layer opacity value",
    .args = {Totem::CommandBackend::detail::arg<Layer>("layer"),
             Totem::CommandBackend::detail::arg<uint32_t>("opacity")},
    .handler = handleLayerOpacityCommand,
    .subcommands = {},
};

inline constexpr CommandDesc layerSwapSubcommand = {
    .name = "swap",
    .description = "Fade-swap two layer opacities",
    .args = {Totem::CommandBackend::detail::arg<Layer>("firstLayer"),
             Totem::CommandBackend::detail::arg<Layer>("secondLayer"),
             Totem::CommandBackend::detail::arg<uint32_t>("durationMs")},
    .handler = handleLayerSwapCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Commands
