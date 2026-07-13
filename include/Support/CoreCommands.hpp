#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
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

// Keep the root-command snapshot off the command task stack while /help logs.
// Command dispatch currently runs serially through CommandCtrl.
inline Totem::CommandBackend::CommandKeySnapshot helpKeySnapshot{};

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

inline ReturnCode
snapshotHelpKeys(const Totem::CommandBackend::detail::ICommandCatalog &catalog) {
    auto keysResult = catalog.snapshotCommandKeys();
    if (!keysResult) {
        return keysResult.error();
    }
    helpKeySnapshot = *keysResult;
    return OK();
}

inline ReturnCode handleHelp(CommandDesc::ParsedArgs /*unused*/, void *ctx) {
    (void)ctx;
    const auto &catalog = CommandCatalogService::get();

    FAIL_IF_ERR_FWD(snapshotHelpKeys(catalog),
                    "Failed to snapshot command names for help");

    _log_i("Registered commands (%zu):", helpKeySnapshot.count);
    for (std::size_t i = 0; i < helpKeySnapshot.count; ++i) {
        const auto &key = helpKeySnapshot.keys[i];
        FAIL_IF_UNEXPECTED_FWD(commandEntry, catalog.get(key),
                               "Failed to get command " SV_FMT
                               " while formatting help",
                               SV_ARG(key.view()));
        FAIL_IF_ERR_FWD(logCommandHelpRecursive(*commandEntry.second, 0),
                        "Failed to format help for command " SV_FMT,
                        SV_ARG(key.view()));
    }

    return OK();
}

} // namespace Totem::Support::detail

inline constinit CommandDesc helpCmd = {
    .name = "help",
    .description = "Output registered command help",
    .args = {},
    .handler = Totem::Support::detail::handleHelp,
    .subcommands = {},
};

inline ReturnCode register_core_commands() {
    auto &reg = CommandRegistrarService::get();

    FAIL_IF_UNEXPECTED_FWD(helloKey, reg.registerCommand(helloCmd),
                           "Failed to register hello command");
    (void)helloKey;
    FAIL_IF_UNEXPECTED_FWD(helpKey, reg.registerCommand(helpCmd),
                           "Failed to register help command");
    (void)helpKey;

    return OK(CoreError);
}
