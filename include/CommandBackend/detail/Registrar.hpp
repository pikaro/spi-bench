#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/Interfaces/Types.hpp"
#include "CommandBackend/detail/Store.hpp"
#include "CommandBackend/detail/Types.hpp" // IWYU pragma: keep
#include "Macros/Facade.hpp"
#include "Services/Commands.hpp"
#include "Types/Error.hpp"
#include <expected>

namespace Totem::CommandBackend::detail {

class Registrar : public IRegistrar {
  public:
    explicit Registrar(Store &store) : _store(store) {}

    std::expected<CommandNameKey, ReturnCode>
    registerCommand(CommandDesc &cmd, void *ctx = nullptr) override {
        FAIL_IF_ERR_FWD_UNEXPECTED(
            cmd.validate(),
            "Invalid command description for command " SV_FMT ":",
            SV_ARG(cmd.name));
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            nameKey, _store.add(ctx, cmd),
            "Failed to add command " SV_FMT " to store:", SV_ARG(cmd.name));
        return nameKey;
    }

    ReturnCode deregisterCommand(const char *name) override {
        FAIL_IF_NULL(name, ERR(InvalidArgument),
                     "Cannot deregister command with null name");
        auto nameKey = CommandNameKey::fromCharPtr(name);
        return deregisterCommand(nameKey);
    }

    ReturnCode deregisterCommand(const CommandNameKey &nameKey) override {
        return _store.remove(nameKey);
    }

  private:
    Store &_store;
};

} // namespace Totem::CommandBackend::detail
