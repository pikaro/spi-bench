#pragma once

#include "CommandBackend/Facade.hpp"
#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <span>

namespace Totem::Output::detail {

template <typename Owner> struct Commands {
    static ReturnCode handle_set_log_level(CommandDesc::ParsedArgs args,
                                           void *ctx) {
        auto *aggregator = static_cast<Owner *>(ctx);

        auto level = args.get<LogLevel>(0);
        FAIL_IF(!level.ok, ERR(CommandError, BadArgument),
                "Missing or invalid log level argument for log command in %s",
                Owner::name);

        auto componentResult = args.get<LogComponent>(1);

        FAIL_IF(!componentResult.ok &&
                    componentResult.error != CommandDesc::ArgError::Missing,
                ERR(CommandError, BadArgument),
                "Invalid log component argument for log command in %s",
                Owner::name);

        std::optional<LogComponent> component;
        if (componentResult.ok) {
            component = componentResult.value;
        }

        return aggregator->setLogLevel(level.value, component);
    }

    static inline CommandDesc logLevelCmd = {
        .needsContext = true,
        .name = "log",
        .description = "Set log level for output",
        .args =
            {
                CommandBackend::arg<LogLevel>("level", false),
                CommandBackend::arg<LogComponent>("component", true),
            },
        .handler = handle_set_log_level,
        .subcommands = {},
    };

    static constexpr std::span<CommandDesc *> commands() {
        static auto commands = std::to_array<CommandDesc *>({
            &logLevelCmd,
        });
        return commands;
    }
};

} // namespace Totem::Output::detail
