#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "CommandBackend/detail/Store.hpp"
#include "LedDisplay/Animations/All.hpp"
#include "LedDisplay/Animations/CenterWaveCommands.hpp"
#include "LedDisplay/Animations/DiagnosticFillCommands.hpp"
#include "LedDisplay/Animations/FftReactiveCommands.hpp"
#include "LedDisplay/Animations/WheelIndicatorCommands.hpp"
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
#include <limits>

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
    return static_cast<uint8_t>(
        std::min<uint32_t>(value, std::numeric_limits<uint8_t>::max()));
}

inline uint16_t clampU16(uint32_t value) {
    return static_cast<uint16_t>(
        std::min<uint32_t>(value, std::numeric_limits<uint16_t>::max()));
}

inline Angle<uint8_t> rawAngleU8(uint32_t value) {
    return Angle<uint8_t>::fromRaw(clampU8(value));
}

inline Angle<uint8_t> degreesAngleU8(uint32_t degrees) {
    constexpr uint32_t degreesPerTurn = 360U;
    return Angle<uint8_t>::fromDeg(
        static_cast<float>(degrees % degreesPerTurn));
}

inline ReturnCode publishCommand(
    std::expected<Totem::LedDisplay::AnimationCommand, ReturnCode> result) {
    FAIL_IF_UNEXPECTED_FWD(cmd, result, "Failed to build animation command");
    return Totem::LedDisplay::publishAnimationCommand(cmd);
}

inline ReturnCode handleAnimRoot(CommandDesc::ParsedArgs /*unused*/,
                                 void * /*unused*/) {
    _log_i("Use /anim wave|fill|fft|wheel|wheel-update|stop|hue|rot");
    return OK();
}

inline ReturnCode handleAnimWave(CommandDesc::ParsedArgs args,
                                 void * /*unused*/) {
    auto config = Totem::LedDisplay::Animations::CenterWaveConfig{};
    FAIL_IF_UNEXPECTED_FWD(duration,
                           optionalU32(args, 0,
                                       Totem::LedDisplay::Animations::
                                           CenterWave::defaultLifetimeMs),
                           "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, optionalU32(args, 2, config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(rise, optionalU32(args, 3, config.rise),
                           "Invalid animation rise argument");
    FAIL_IF_UNEXPECTED_FWD(peak, optionalU32(args, 4, config.peak),
                           "Invalid animation peak argument");
    FAIL_IF_UNEXPECTED_FWD(wake, optionalU32(args, 5, config.wake),
                           "Invalid animation wake argument");

    config.hue = clampU8(hue);
    config.value = clampU8(value);
    config.rise = clampU8(rise);
    config.peak = std::max<uint8_t>(
        clampU8(peak),
        Totem::LedDisplay::Animations::CenterWave::minimumPeakRings);
    config.wake = clampU8(wake);

    return publishCommand(Totem::LedDisplay::Animations::CenterWave::makeCommand(
        config, 0, clampU16(duration)));
}

inline ReturnCode handleAnimFill(CommandDesc::ParsedArgs args,
                                 void * /*unused*/) {
    auto config = Totem::LedDisplay::Animations::DiagnosticFillConfig{};
    FAIL_IF_UNEXPECTED_FWD(duration,
                           optionalU32(args, 0,
                                       Totem::LedDisplay::Animations::
                                           DiagnosticFill::defaultLifetimeMs),
                           "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, optionalU32(args, 1, config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, optionalU32(args, 2, config.value),
                           "Invalid animation value argument");

    config.hue = clampU8(hue);
    config.value = clampU8(value);

    return publishCommand(
        Totem::LedDisplay::Animations::DiagnosticFill::makeCommand(
            config, 0, clampU16(duration)));
}

inline ReturnCode handleAnimFft(CommandDesc::ParsedArgs args,
                                void * /*unused*/) {
    auto config = Totem::LedDisplay::Animations::FftReactiveConfig{};
    FAIL_IF_UNEXPECTED_FWD(duration,
                           optionalU32(args, 0,
                                       Totem::LedDisplay::Animations::
                                           FftReactive::defaultLifetimeMs),
                           "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, optionalU32(args, 1, config.baseHue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, optionalU32(args, 2, config.valueScale),
                           "Invalid animation value argument");

    config.baseHue = clampU8(hue);
    config.valueScale = clampU8(value);

    return publishCommand(
        Totem::LedDisplay::Animations::FftReactive::makeCommand(
            config, 0, clampU16(duration)));
}

inline std::expected<Totem::LedDisplay::Animations::WheelIndicatorConfig,
                     ReturnCode>
parseWheelIndicatorConfig(CommandDesc::ParsedArgs args, size_t firstIndex) {
    auto config = Totem::LedDisplay::Animations::WheelIndicatorConfig{};
    FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(hue, optionalU32(args, firstIndex,
                                                      config.hue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(value, optionalU32(args, firstIndex + 1U,
                                                        config.value),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(spokes,
                                      optionalU32(args, firstIndex + 2U,
                                                  config.spokes),
                           "Invalid animation spokes argument");
    FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(falloff,
                                      optionalU32(args, firstIndex + 3U,
                                                  config.falloff),
                           "Invalid animation falloff argument");

    config.hue = clampU8(hue);
    config.value = clampU8(value);
    config.spokes = std::max<uint8_t>(
        clampU8(spokes),
        Totem::LedDisplay::Animations::WheelIndicator::minimumSpokes);
    config.falloff = clampU8(falloff);
    return config;
}

inline ReturnCode handleAnimWheel(CommandDesc::ParsedArgs args,
                                  void * /*unused*/) {
    FAIL_IF_UNEXPECTED_FWD(duration,
                           optionalU32(args, 0,
                                       Totem::LedDisplay::Animations::
                                           WheelIndicator::defaultLifetimeMs),
                           "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(config, parseWheelIndicatorConfig(args, 1),
                           "Invalid wheel indicator config arguments");

    return publishCommand(
        Totem::LedDisplay::Animations::WheelIndicator::makeCommand(
            config, 0, clampU16(duration)));
}

inline ReturnCode handleAnimWheelUpdate(CommandDesc::ParsedArgs args,
                                        void * /*unused*/) {
    FAIL_IF_UNEXPECTED_FWD(config, parseWheelIndicatorConfig(args, 0),
                           "Invalid wheel indicator update arguments");
    FAIL_IF_UNEXPECTED_FWD(
        requestId,
        optionalU32(args, 4,
                    Totem::LedDisplay::Animations::WheelIndicator::
                        defaultRequestId),
        "Invalid wheel indicator request ID argument");

    return publishCommand(
        Totem::LedDisplay::Animations::WheelIndicator::makeUpdateCommand(
            config, clampU16(requestId)));
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

inline std::array<CommandDesc, 8> animSubcommands{{
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
                     "rise", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "peak", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "wake", CommandDesc::ArgRequirement::Optional)},
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
                     "falloff", CommandDesc::ArgRequirement::Optional)},
        .handler = Totem::Support::detail::handleAnimWheel,
        .subcommands = {},
    },
    {
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
        .handler = Totem::Support::detail::handleAnimWheelUpdate,
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
