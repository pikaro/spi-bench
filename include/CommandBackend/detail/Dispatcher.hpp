#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Types.hpp" // IWYU pragma: keep
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <cstring>

namespace Totem::CommandBackend::detail {

class Dispatcher {
  public:
    static ReturnCode dispatch(const CommandDesc &command, void *ctx,
                               CommandDesc::Tokens args) {
        size_t requiredArgs = 0;
        for (const auto &arg : command.args) {
            if (arg.name.empty() || arg.optional) {
                break;
            }
            ++requiredArgs;
        }

        if (args.size() < requiredArgs) {
            FAIL(ERR(CommandError, BadArgumentCount),
                 "Command %s requires at least %zu arguments, but got %zu",
                 command.name, requiredArgs, args.size());
        }

        for (const auto &subcommand : command.subcommands) {
            if (args.size() > 0 && args[0] == subcommand.name) {
                return dispatch(subcommand, ctx, args.subspan(1));
            }
        }

        auto parser = CommandDesc::ParsedArgs{.desc = command, .tokens = args};

        return command.handler(parser, ctx);
    }
};

} // namespace Totem::CommandBackend::detail
