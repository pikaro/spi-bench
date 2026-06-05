#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Animations/Bolt/Command.hpp"
#include "LedDisplay/Animations/detail/CommandCliHelpers.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

inline ReturnCode handleBoltCommand(CommandDesc::ParsedArgs args,
                                    void * /*unused*/) {
    auto config = BoltConfig{};
    FAIL_IF_UNEXPECTED_FWD(
        duration, detail::optionalU32(args, 0, BoltCommand::defaultLifetimeMs),
        "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, detail::optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, detail::optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(width, detail::optionalU32(args, 3, config.width),
                           "Invalid animation width argument");
    FAIL_IF_UNEXPECTED_FWD(jitter, detail::optionalU32(args, 4, config.jitter),
                           "Invalid animation jitter argument");
    FAIL_IF_UNEXPECTED_FWD(forks, detail::optionalU32(args, 5, config.forks),
                           "Invalid animation fork argument");
    FAIL_IF_UNEXPECTED_FWD(seed, detail::optionalU32(args, 6, config.seed),
                           "Invalid animation seed argument");
    FAIL_IF_UNEXPECTED_FWD(outerOrigin,
                           detail::optionalU32(args, 7, config.outerOrigin),
                           "Invalid animation origin argument");

    config.hue = detail::clampU8(hue);
    config.value = detail::clampU8(value);
    config.width =
        std::max<uint8_t>(detail::clampU8(width), BoltCommand::minimumWidth);
    config.jitter = detail::clampU8(jitter);
    config.forks = detail::clampU8(forks);
    config.seed = detail::clampU8(seed);
    config.outerOrigin = outerOrigin != 0;

    return detail::publishCommand(
        BoltCommand::makeCommand(config, 0, detail::clampU16(duration)));
}

inline constexpr CommandDesc boltSubcommand = {
    .name = "bolt",
    .description = "Publish a deterministic jagged bolt animation",
    .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                 "durationMs", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "hue", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "value", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "width", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "jitter", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "forks", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "seed", CommandDesc::ArgRequirement::Optional),
             Totem::CommandBackend::detail::arg<uint32_t>(
                 "outerOrigin", CommandDesc::ArgRequirement::Optional)},
    .handler = handleBoltCommand,
    .subcommands = {},
};

} // namespace Totem::LedDisplay::Animations
