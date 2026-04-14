#pragma once

#include "Macros/Facade.hh"
#include "Types/Command.hh"
#include "Types/Error.hh"
#include <array>
#include <magic_enum/magic_enum.hpp>
#include <span>

namespace Totem::Output::detail {

template <typename Owner> struct Commands {
    static ReturnCode handle_set_log_level(CommandDesc::Tokens args,
                                           void *ctx) {
        auto *aggregator = static_cast<Owner *>(ctx);
        FAIL_IF(args.size() != 1, ERR(CommandError, BadArgumentCount),
                "set_log_level command requires exactly 1 argument");
        FAIL_IF_UNEXPECTED(logLevel, log_level_from_string(args[0]),
                           ERR(CommandError, BadArgument),
                           "Invalid log level string: " SV_FMT,
                           SV_ARG(args[0]));
        return aggregator->setLogLevel(logLevel);
    }

    CommandDesc logLevelCmd = {
        .needsContext = true,
        .name = "log",
        .description = "Set log level for output",
        .args = {},
        .minArgs = 0,
        .handler = handle_set_log_level,
        .subcommands = {},
    };

    std::span<CommandDesc *> commands() {
        static auto commands = std::to_array<CommandDesc *>({
            &logLevelCmd,
        });
        return commands;
    }
};

} // namespace Totem::Output::detail
