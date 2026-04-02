#pragma once

#include "Macros/Facade.hh"
#include "Types/Command.hh"
#include "Types/Error.hh"
#include <cstring>

namespace Totem::CommandBackend::detail {

class Dispatcher {
  public:
    static ReturnCode dispatch(const CommandDesc &command,
                               CommandDesc::Tokens args) {
        if (args.size() < command.minArgs) {
            _log_e("Command %s requires at least %zu arguments, but got %zu",
                   command.name, command.minArgs, args.size());
            return ERR(InvalidArgument);
        }

        for (const auto &subcommand : command.subcommands) {
            if (args.size() > 0 && args[0] == subcommand.name) {
                return dispatch(subcommand, args.subspan(1));
            }
        }

        return command.handler(args);
    }

  private:
    using DefaultError = CoreError;
};

} // namespace Totem::CommandBackend::detail
