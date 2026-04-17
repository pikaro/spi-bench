#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Store.hpp"
#include "CommandBackend/detail/Types.hpp" // IWYU pragma: keep
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <expected>

namespace Totem::CommandBackend::detail {

class Registrar {
  public:
    explicit Registrar(Store &store) : _store(store) {}

    std::expected<Store::CommandNameKey, ReturnCode>
    registerCommand(CommandDesc &cmd, void *ctx = nullptr) {
        FAIL_IF_ERR_FWD_UNEXPECTED(
            cmd.validate(),
            "Invalid command description for command %s:", cmd.name);
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            nameKey, _store.add(cmd.name.data(), ctx, cmd),
            "Failed to add command %s to store:", cmd.name);
        return nameKey;
    }

    ReturnCode deregisterCommand(const char *name) {
        FAIL_IF_NULL(name, ERR(InvalidArgument),
                     "Cannot deregister command with null name");
        auto nameKey = Store::CommandNameKey::fromCharPtr(name);
        return deregisterCommand(nameKey);
    }

    ReturnCode deregisterCommand(const Store::CommandNameKey &nameKey) {
        return _store.remove(nameKey);
    }

  private:
    Store &_store;
};

} // namespace Totem::CommandBackend::detail
