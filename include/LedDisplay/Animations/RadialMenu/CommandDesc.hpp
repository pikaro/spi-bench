#pragma once

#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/RadialMenu/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleRadialMenuCommand(CommandDesc::ParsedArgs args,
                                          void * /*unused*/) {
    auto config = RadialMenuConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration,
        detail::optionalU32(args, 0, RadialMenuCommand::defaultLifetimeMs),
        "Invalid radial menu duration argument");
    FAIL_IF_UNEXPECTED_FWD(selectedItem,
                           detail::optionalU32(args, 1, config.selectedItem),
                           "Invalid radial menu selected item argument");
    FAIL_IF_UNEXPECTED_FWD(itemCount,
                           detail::optionalU32(args, 2, config.itemCount),
                           "Invalid radial menu item count argument");
    FAIL_IF_UNEXPECTED_FWD(populatedItems,
                           detail::optionalU32(args, 3, config.populatedItems),
                           "Invalid radial menu populated-items argument");
    FAIL_IF_UNEXPECTED_FWD(baseSpokeWidth,
                           detail::optionalU32(args, 4, config.baseSpokeWidth),
                           "Invalid radial menu base spoke width argument");
    FAIL_IF_UNEXPECTED_FWD(baseRingDepth,
                           detail::optionalU32(args, 5, config.baseRingDepth),
                           "Invalid radial menu base ring depth argument");
    FAIL_IF_UNEXPECTED_FWD(
        baseTipSpokeWidth,
        detail::optionalU32(args, 6, config.baseTipSpokeWidth),
        "Invalid radial menu base tip spoke width argument");
    FAIL_IF_UNEXPECTED_FWD(
        baseTipRingDepth, detail::optionalU32(args, 7, config.baseTipRingDepth),
        "Invalid radial menu base tip ring depth argument");
    FAIL_IF_UNEXPECTED_FWD(
        unfurledSpokeWidth,
        detail::optionalU32(args, 8, config.unfurledSpokeWidth),
        "Invalid radial menu unfurled spoke width argument");
    FAIL_IF_UNEXPECTED_FWD(
        unfurledRingDepth,
        detail::optionalU32(args, 9, config.unfurledRingDepth),
        "Invalid radial menu unfurled ring depth argument");
    FAIL_IF_UNEXPECTED_FWD(
        unfurledTipSpokeWidth,
        detail::optionalU32(args, 10, config.unfurledTipSpokeWidth),
        "Invalid radial menu unfurled tip spoke width argument");
    FAIL_IF_UNEXPECTED_FWD(
        unfurledTipRingDepth,
        detail::optionalU32(args, 11, config.unfurledTipRingDepth),
        "Invalid radial menu unfurled tip ring depth argument");
    FAIL_IF_UNEXPECTED_FWD(
        unfurlDurationMs,
        detail::optionalU32(args, 12, config.unfurlDurationMs),
        "Invalid radial menu unfurl duration argument");
    FAIL_IF_UNEXPECTED_FWD(
        requestId,
        detail::optionalU32(args, 13, RadialMenuCommand::defaultRequestId),
        "Invalid radial menu request ID argument");

    config.selectedItem = detail::clampU8(selectedItem);
    config.itemCount = static_cast<uint8_t>(
        std::min<uint32_t>(itemCount, RadialMenuSpec::maximumItems));
    config.populatedItems = detail::clampU8(populatedItems);
    config.baseSpokeWidth = detail::clampU8(baseSpokeWidth);
    config.baseRingDepth = detail::clampU8(baseRingDepth);
    config.baseTipSpokeWidth = detail::clampU8(baseTipSpokeWidth);
    config.baseTipRingDepth = detail::clampU8(baseTipRingDepth);
    config.unfurledSpokeWidth = detail::clampU8(unfurledSpokeWidth);
    config.unfurledRingDepth = detail::clampU8(unfurledRingDepth);
    config.unfurledTipSpokeWidth = detail::clampU8(unfurledTipSpokeWidth);
    config.unfurledTipRingDepth = detail::clampU8(unfurledTipRingDepth);
    config.unfurlDurationMs = detail::clampU16(unfurlDurationMs);

    return detail::publishCommand(RadialMenuCommand::makeCommand(
        config, detail::clampU16(requestId), detail::clampU16(duration)));
}

inline constexpr CommandDesc radialMenuSubcommand = {
    .name = "menu",
    .description = "Publish a configurable radial menu",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "selectedItem", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "itemCount", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "populatedItems", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "baseSpokeWidth", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "baseRingDepth", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "baseTipSpokeWidth", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "baseTipRingDepth", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "unfurledSpokeWidth", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "unfurledRingDepth", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "unfurledTipSpokeWidth",
                 CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "unfurledTipRingDepth", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "unfurlDurationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "requestId", CommandDesc::ArgRequirement::Optional)},
    .handler = handleRadialMenuCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
