#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "CommandBackend/detail/Store.hpp"
#include "LedDisplay/Interfaces/AnimationCommandFactory.hpp"
#include "Macros/Facade.hpp"
#include "Services/Commands.hpp"
#include "Types/Angle.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

inline CommandDesc helloCmd = {
    .name = "hello",
    .description = "Prints Hello, World!",
    .args = {},
    .handler = [](CommandDesc::ParsedArgs /*unused*/, void *) -> ReturnCode {
        _log_i("Hello, World!");
        return OK(CoreError);
    },
    .subcommands = {},
};

namespace Totem::Support::detail {

using CommandStore = Totem::CommandBackend::detail::Store;

// Keep the root-command snapshot off the command task stack while /help logs.
// Command dispatch currently runs serially through CommandCtrl.
inline CommandStore::CommandKeySnapshot helpKeySnapshot{};

inline void logHelpArgument(const CommandDesc::Argument &arg,
                            std::size_t depth) {
    const bool optional =
        arg.requirement == CommandDesc::ArgRequirement::Optional;
    _log_i("%*sarg %s" SV_FMT "%s", static_cast<int>((depth + 1U) * 2U), "",
           optional ? "[" : "<", SV_ARG(arg.name), optional ? "]" : ">");
}

inline void logHelpArguments(const CommandDesc &command, std::size_t depth) {
    for (const auto &arg : command.args) {
        if (arg.name.empty()) {
            break;
        }
        logHelpArgument(arg, depth);
    }
}

inline ReturnCode logCommandHelpRecursive(const CommandDesc &command,
                                          std::size_t depth) {
    if (depth >= CommandConfig::maxTokens) {
        _log_w("Command help depth limit reached below " SV_FMT,
               SV_ARG(command.name));
        return OK();
    }

    _log_i("%*s%s" SV_FMT " - %s", static_cast<int>(depth * 2U), "",
           depth == 0 ? "/" : "", SV_ARG(command.name), command.description);
    logHelpArguments(command, depth);

    for (const auto &subcommand : command.subcommands) {
        auto subcommandRet = logCommandHelpRecursive(subcommand, depth + 1U);
        if (!subcommandRet.ok()) {
            return subcommandRet;
        }
    }

    return OK();
}

inline ReturnCode snapshotHelpKeys(const CommandStore &store) {
    auto keysResult = store.snapshotCommandKeys();
    if (!keysResult) {
        return keysResult.error();
    }
    helpKeySnapshot = *keysResult;
    return OK();
}

inline ReturnCode handleHelp(CommandDesc::ParsedArgs /*unused*/, void *ctx) {
    const auto *store = static_cast<const CommandStore *>(ctx);
    FAIL_IF_NULL(store, ERR(CoreError, InvalidArgument),
                 "Command help context is null");

    FAIL_IF_ERR_FWD(snapshotHelpKeys(*store),
                    "Failed to snapshot command names for help");

    _log_i("Registered commands (%zu):", helpKeySnapshot.count);
    for (std::size_t i = 0; i < helpKeySnapshot.count; ++i) {
        const auto &key = helpKeySnapshot.keys[i];
        FAIL_IF_UNEXPECTED_FWD(commandEntry, store->get(key),
                               "Failed to get command " SV_FMT
                               " while formatting help",
                               SV_ARG(key.view()));
        FAIL_IF_ERR_FWD(logCommandHelpRecursive(commandEntry.second, 0),
                        "Failed to format help for command " SV_FMT,
                        SV_ARG(key.view()));
    }

    return OK();
}

inline std::expected<uint32_t, ReturnCode>
optionalU32(CommandDesc::ParsedArgs args, size_t index,
            uint32_t defaultValue) {
    auto parsed = args.get<uint32_t>(index);
    if (parsed.ok) {
        return parsed.value;
    }
    if (parsed.error == CommandDesc::ArgError::Missing) {
        return defaultValue;
    }
    return std::unexpected(ERR(CoreError, InvalidArgument));
}

inline uint8_t clampU8(uint32_t value) {
    return static_cast<uint8_t>(std::min<uint32_t>(value, 255U));
}

inline uint16_t clampU16(uint32_t value) {
    return static_cast<uint16_t>(std::min<uint32_t>(value, 65535U));
}

inline Angle<uint8_t> rawAngleU8(uint32_t value) {
    return Angle<uint8_t>::fromRaw(clampU8(value));
}

inline Angle<uint8_t> degreesAngleU8(uint32_t degrees) {
    return Angle<uint8_t>::fromDeg(static_cast<float>(degrees % 360U));
}

inline ReturnCode publishAnim(CommandDesc::ParsedArgs args,
                              Totem::LedDisplay::AnimationKind kind,
                              uint32_t defaultDuration,
                              uint32_t defaultHue,
                              uint32_t defaultValue,
                              uint32_t defaultParam) {
    FAIL_IF_UNEXPECTED_FWD(duration,
                           optionalU32(args, 0, defaultDuration),
                           "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, optionalU32(args, 1, defaultHue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, optionalU32(args, 2, defaultValue),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(param, optionalU32(args, 3, defaultParam),
                           "Invalid animation parameter argument");

    return Totem::LedDisplay::publishAnimation({
        .kind = kind,
        .lifetimeMs = clampU16(duration),
        .hue = clampU8(hue),
        .value = clampU8(value),
        .param = clampU8(param),
    });
}

inline ReturnCode handleAnimRoot(CommandDesc::ParsedArgs /*unused*/,
                                 void * /*unused*/) {
    _log_i("Use /anim wave|fill|fft|prim|stop|hue|rot");
    return OK();
}

inline ReturnCode handleAnimWave(CommandDesc::ParsedArgs args,
                                 void * /*unused*/) {
    return publishAnim(args, Totem::LedDisplay::AnimationKind::CenterWave,
                       1200, 144, 180, 5);
}

inline ReturnCode handleAnimFill(CommandDesc::ParsedArgs args,
                                 void * /*unused*/) {
    return publishAnim(args, Totem::LedDisplay::AnimationKind::DiagnosticFill,
                       2000, 96, 80, 0);
}

inline ReturnCode handleAnimFft(CommandDesc::ParsedArgs args,
                                void * /*unused*/) {
    return publishAnim(args, Totem::LedDisplay::AnimationKind::FftReactive, 0,
                       0, 180, 0);
}

inline ReturnCode handleAnimPrim(CommandDesc::ParsedArgs args,
                                 void * /*unused*/) {
    auto primitive = args.get<Totem::LedDisplay::PrimitiveKind>(0);
    FAIL_IF_NOT(primitive.ok, ERR(CoreError, InvalidArgument),
                "Invalid primitive kind argument");
    FAIL_IF_UNEXPECTED_FWD(duration, optionalU32(args, 1, 2400),
                           "Invalid primitive duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, optionalU32(args, 2, 144),
                           "Invalid primitive hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, optionalU32(args, 3, 180),
                           "Invalid primitive value argument");
    FAIL_IF_UNEXPECTED_FWD(width, optionalU32(args, 4, 5),
                           "Invalid primitive width argument");

    return Totem::LedDisplay::publishAnimation({
        .kind = Totem::LedDisplay::AnimationKind::PrimitiveDemo,
        .primitive = primitive.value,
        .lifetimeMs = clampU16(duration),
        .hue = clampU8(hue),
        .value = clampU8(value),
        .param = clampU8(width),
    });
}

inline ReturnCode handleAnimStop(CommandDesc::ParsedArgs args,
                                 void * /*unused*/) {
    FAIL_IF_UNEXPECTED_FWD(requestId, optionalU32(args, 0, 0),
                           "Invalid animation request ID argument");
    return Totem::LedDisplay::publishAnimationCommand(
        Totem::LedDisplay::makeStopAnimationCommand(clampU16(requestId)));
}

inline ReturnCode handleAnimHue(CommandDesc::ParsedArgs args,
                                void * /*unused*/) {
    auto parsed = args.get<uint32_t>(0);
    FAIL_IF_NOT(parsed.ok, ERR(CoreError, InvalidArgument),
                "Invalid hue offset argument");
    FAIL_IF_UNEXPECTED_FWD(cmd,
                           Totem::LedDisplay::makeHueOffsetCommand(
                               rawAngleU8(parsed.value)),
                           "Failed to build LED hue offset command");
    return Totem::LedDisplay::publishAnimationCommand(cmd);
}

inline ReturnCode handleAnimRot(CommandDesc::ParsedArgs args,
                                void * /*unused*/) {
    auto parsed = args.get<uint32_t>(0);
    FAIL_IF_NOT(parsed.ok, ERR(CoreError, InvalidArgument),
                "Invalid rotation offset argument");
    FAIL_IF_UNEXPECTED_FWD(cmd,
                           Totem::LedDisplay::makeRotationOffsetCommand(
                               degreesAngleU8(parsed.value)),
                           "Failed to build LED rotation offset command");
    return Totem::LedDisplay::publishAnimationCommand(cmd);
}

} // namespace Totem::Support::detail

inline std::array<CommandDesc, 7> animSubcommands{{
    {
        .name = "wave",
        .description = "Publish a center wave animation",
        .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                     "durationMs", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "hue", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "value", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "width", CommandDesc::ArgRequirement::Optional)},
        .handler = Totem::Support::detail::handleAnimWave,
        .subcommands = {},
    },
    {
        .name = "fill",
        .description = "Publish a diagnostic fill animation",
        .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                     "durationMs", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "hue", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "value", CommandDesc::ArgRequirement::Optional)},
        .handler = Totem::Support::detail::handleAnimFill,
        .subcommands = {},
    },
    {
        .name = "fft",
        .description = "Publish the persistent FFT reactive animation",
        .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                     "durationMs", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "hue", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "valueScale", CommandDesc::ArgRequirement::Optional)},
        .handler = Totem::Support::detail::handleAnimFft,
        .subcommands = {},
    },
    {
        .name = "prim",
        .description = "Publish a primitive demo animation",
        .args = {Totem::CommandBackend::detail::arg<
                     Totem::LedDisplay::PrimitiveKind>("primitive"),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "durationMs", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "hue", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "value", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "width", CommandDesc::ArgRequirement::Optional)},
        .handler = Totem::Support::detail::handleAnimPrim,
        .subcommands = {},
    },
    {
        .name = "stop",
        .description = "Publish an animation stop command",
        .args = {Totem::CommandBackend::detail::arg<uint32_t>(
            "requestId", CommandDesc::ArgRequirement::Optional)},
        .handler = Totem::Support::detail::handleAnimStop,
        .subcommands = {},
    },
    {
        .name = "hue",
        .description = "Publish a global LED hue offset",
        .args = {Totem::CommandBackend::detail::arg<uint32_t>("offset")},
        .handler = Totem::Support::detail::handleAnimHue,
        .subcommands = {},
    },
    {
        .name = "rot",
        .description = "Publish a global LED rotation offset in degrees",
        .args = {Totem::CommandBackend::detail::arg<uint32_t>("degrees")},
        .handler = Totem::Support::detail::handleAnimRot,
        .subcommands = {},
    },
}};

inline CommandDesc animCmd = {
    .name = "anim",
    .description = "Publish LED display commands over PubSub",
    .args = {},
    .handler = Totem::Support::detail::handleAnimRoot,
    .subcommands = animSubcommands,
};

inline CommandDesc helpCmd = {
    .needsContext = true,
    .name = "help",
    .description = "Output registered command help",
    .args = {},
    .handler = Totem::Support::detail::handleHelp,
    .subcommands = {},
};

inline ReturnCode
register_core_commands(const Totem::CommandBackend::detail::Store &store) {
    auto &reg = CommandRegistrarService::get();

    FAIL_IF_UNEXPECTED_FWD(helloKey, reg.registerCommand(helloCmd),
                           "Failed to register hello command");
    (void)helloKey;
    FAIL_IF_UNEXPECTED_FWD(animKey, reg.registerCommand(animCmd),
                           "Failed to register anim command");
    (void)animKey;
    // Command contexts are void*; /help casts this back to const and only
    // reads the store.
    auto *helpContext =
        const_cast<Totem::CommandBackend::detail::Store *>(&store);
    FAIL_IF_UNEXPECTED_FWD(helpKey, reg.registerCommand(helpCmd, helpContext),
                           "Failed to register help command");
    (void)helpKey;

    return OK(CoreError);
}
