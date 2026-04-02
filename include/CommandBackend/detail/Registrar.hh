#pragma once

#include "CommandBackend/detail/Store.hh"
#include "Macros/Facade.hh"
#include "Types/Command.hh"
#include "Types/Error.hh"

namespace Totem::CommandBackend::detail {

class Registrar {
  public:
    explicit Registrar(Store &store) : _store(store) {}

    ReturnCode registerCommand(const CommandDesc &cmd) {
        FAIL_IF_ERR_FWD(
            cmd.validate(),
            "Invalid command description for command %s:", cmd.name);
        FAIL_IF_UNEXPECTED_FWD(nameKey, _store.add(cmd.name, cmd),
                               "Failed to add command %s to store:", cmd.name);
        (void)nameKey;
        return OK();
    }

  private:
    Store &_store;

    using DefaultError = CoreError;
};

} // namespace Totem::CommandBackend::detail
