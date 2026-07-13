#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/Interfaces/ICommandCatalog.hpp"
#include "CommandBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <expected>

namespace Totem::CommandBackend::detail {

struct IRegistrar {
    virtual ~IRegistrar() = default;

    virtual std::expected<CommandNameKey, ReturnCode>
    registerCommand(CommandDesc &cmd, void *ctx = nullptr) = 0;
    virtual ReturnCode deregisterCommand(const char *name) = 0;
    virtual ReturnCode deregisterCommand(const CommandNameKey &nameKey) = 0;
};

} // namespace Totem::CommandBackend::detail

class CommandRegistrarService {
    using IRegistrar = Totem::CommandBackend::detail::IRegistrar;

    inline static IRegistrar *_registrar;

  public:
    /**
     * @brief Installs the command backend used by the service facade.
     * @param ctrl Backend controller instance to expose through CommandService.
     */
    static void set(IRegistrar &registrar) { _registrar = &registrar; }

    static IRegistrar &get() {
        ABORT_IF(_registrar == nullptr, "Command backend controller not set");
        return *_registrar;
    }
};

class CommandCatalogService {
    using ICommandCatalog = Totem::CommandBackend::detail::ICommandCatalog;

    inline static const ICommandCatalog *_catalog;

  public:
    static void set(const ICommandCatalog &catalog) { _catalog = &catalog; }

    static const ICommandCatalog &get() {
        ABORT_IF(_catalog == nullptr, "Command catalog not set");
        return *_catalog;
    }
};
