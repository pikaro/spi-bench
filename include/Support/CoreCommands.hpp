#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Store.hpp"
#include "LedDisplay/Animations/AnimationStopCommandDesc.hpp"
#include "LedDisplay/Animations/Bolt/CommandDesc.hpp"
#include "LedDisplay/Animations/BreathingRings/CommandDesc.hpp"
#include "LedDisplay/Animations/CenterWave/CommandDesc.hpp"
#include "LedDisplay/Animations/Cymatic/CommandDesc.hpp"
#include "LedDisplay/Animations/DiagnosticFill/CommandDesc.hpp"
#include "LedDisplay/Animations/Lighthouse/CommandDesc.hpp"
#include "LedDisplay/Animations/OrbitRing/CommandDesc.hpp"
#include "LedDisplay/Animations/OrbitSparks/CommandDesc.hpp"
#include "LedDisplay/Animations/PolarLattice/CommandDesc.hpp"
#include "LedDisplay/Animations/RadialCurtain/CommandDesc.hpp"
#include "LedDisplay/Animations/Shutter/CommandDesc.hpp"
#include "LedDisplay/Animations/SineWave/CommandDesc.hpp"
#include "LedDisplay/Animations/Sinelon/CommandDesc.hpp"
#include "LedDisplay/Animations/SpectralIris/CommandDesc.hpp"
#include "LedDisplay/Animations/SpectralWeave/CommandDesc.hpp"
#include "LedDisplay/Animations/SpokeSweep/CommandDesc.hpp"
#include "LedDisplay/Animations/StainedCells/CommandDesc.hpp"
#include "LedDisplay/Animations/Starburst/CommandDesc.hpp"
#include "LedDisplay/Animations/Vortex/CommandDesc.hpp"
#include "LedDisplay/Animations/WheelIndicator/CommandDesc.hpp"
#include "LedDisplay/Commands/DisplayCommandDesc.hpp"
#include "LedDisplay/Commands/LayerCommandDesc.hpp"
#include "Macros/Facade.hpp"
#include "Services/Commands.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>

inline constinit CommandDesc helloCmd = {
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

inline ReturnCode handleAnimRoot(CommandDesc::ParsedArgs /*unused*/,
                                 void * /*unused*/) {
    _log_i("Use /anim "
           "wave|starburst|vortex|shutter|orbit|lighthouse|cymatic|rings|"
           "curtain|lattice|bolt|sinelon|sine|fill|weave|iris|sparks|cells|"
           "sweep|wheel|wheel-update|stop");
    return OK();
}

inline ReturnCode handleDispRoot(CommandDesc::ParsedArgs /*unused*/,
                                 void * /*unused*/) {
    _log_i("Use /disp hue|rot|brightness");
    return OK();
}

inline ReturnCode handleLayerRoot(CommandDesc::ParsedArgs /*unused*/,
                                  void * /*unused*/) {
    _log_i("Use /layer active|opacity|swap");
    return OK();
}

} // namespace Totem::Support::detail

inline constinit std::array<CommandDesc, 22> animSubcommands{{
    Totem::LedDisplay::Animations::centerWaveSubcommand,
    Totem::LedDisplay::Animations::starburstSubcommand,
    Totem::LedDisplay::Animations::vortexSubcommand,
    Totem::LedDisplay::Animations::shutterSubcommand,
    Totem::LedDisplay::Animations::orbitRingSubcommand,
    Totem::LedDisplay::Animations::lighthouseSubcommand,
    Totem::LedDisplay::Animations::cymaticSubcommand,
    Totem::LedDisplay::Animations::breathingRingsSubcommand,
    Totem::LedDisplay::Animations::radialCurtainSubcommand,
    Totem::LedDisplay::Animations::polarLatticeSubcommand,
    Totem::LedDisplay::Animations::boltSubcommand,
    Totem::LedDisplay::Animations::diagnosticFillSubcommand,
    Totem::LedDisplay::Animations::sinelonSubcommand,
    Totem::LedDisplay::Animations::sineWaveSubcommand,
    Totem::LedDisplay::Animations::spectralWeaveSubcommand,
    Totem::LedDisplay::Animations::spectralIrisSubcommand,
    Totem::LedDisplay::Animations::orbitSparksSubcommand,
    Totem::LedDisplay::Animations::stainedCellsSubcommand,
    Totem::LedDisplay::Animations::spokeSweepSubcommand,
    Totem::LedDisplay::Animations::wheelIndicatorSubcommand,
    Totem::LedDisplay::Animations::wheelIndicatorUpdateSubcommand,
    Totem::LedDisplay::Animations::animationStopSubcommand,
}};

inline constinit CommandDesc animCmd = {
    .name = "anim",
    .description = "Publish LED display commands over PubSub",
    .args = {},
    .handler = Totem::Support::detail::handleAnimRoot,
    .subcommands = animSubcommands,
};

inline constinit std::array<CommandDesc, 3> dispSubcommands{{
    Totem::LedDisplay::Commands::hueOffsetSubcommand,
    Totem::LedDisplay::Commands::rotationOffsetSubcommand,
    Totem::LedDisplay::Commands::brightnessSubcommand,
}};

inline constinit CommandDesc dispCmd = {
    .name = "disp",
    .description = "Publish LED display controls over PubSub",
    .args = {},
    .handler = Totem::Support::detail::handleDispRoot,
    .subcommands = dispSubcommands,
};

inline constinit std::array<CommandDesc, 3> layerSubcommands{{
    Totem::LedDisplay::Commands::layerActiveSubcommand,
    Totem::LedDisplay::Commands::layerOpacitySubcommand,
    Totem::LedDisplay::Commands::layerSwapSubcommand,
}};

inline constinit CommandDesc layerCmd = {
    .name = "layer",
    .description = "Publish LED layer controls over PubSub",
    .args = {},
    .handler = Totem::Support::detail::handleLayerRoot,
    .subcommands = layerSubcommands,
};

inline constinit CommandDesc helpCmd = {
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
    FAIL_IF_UNEXPECTED_FWD(dispKey, reg.registerCommand(dispCmd),
                           "Failed to register disp command");
    (void)dispKey;
    FAIL_IF_UNEXPECTED_FWD(layerKey, reg.registerCommand(layerCmd),
                           "Failed to register layer command");
    (void)layerKey;
    // Command contexts are void*; /help casts this back to const and only
    // reads the store.
    auto *helpContext =
        const_cast<Totem::CommandBackend::detail::Store *>(&store);
    FAIL_IF_UNEXPECTED_FWD(helpKey, reg.registerCommand(helpCmd, helpContext),
                           "Failed to register help command");
    (void)helpKey;

    return OK(CoreError);
}
