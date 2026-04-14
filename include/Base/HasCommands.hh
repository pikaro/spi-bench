#pragma once

#include "CommandBackend/detail/Store.hh"
#include "Concepts/Base.hh"
#include "Macros/Facade.hh"
#include "Services/Commands.hh"
#include "StaticConfig/Command.hh"
#include "Types/Command.hh"
#include "Types/Error.hh"
#include <array>
#include <concepts>
#include <cstdint>
#include <span>

template <class Derived, typename CommandSet> class HasCommands {
  protected:
    ReturnCode _registerCommands() {
        auto &registrar = CommandService::registrar();
        auto cmdSet = CommandSet();
        for (auto *cmd : cmdSet.commands()) {
            FAIL_IF(_registeredCommandCount >=
                        CommandConfig::maxEntriesPerClass,
                    ERR(OutOfMemory),
                    "Exceeded maximum number of commands per class for %s",
                    Derived::name);
            auto *self = static_cast<Derived *>(this);
            FAIL_IF_NULL(cmd, ERR(InvalidArgument),
                         "Command set for %s contains null command pointer",
                         Derived::name);
            FAIL_IF_UNEXPECTED_FWD(nameKey,
                                   registrar.registerCommand(*cmd, self),
                                   "Failed to register command %s for %s",
                                   cmd->name, Derived::name);
            _registeredCommandKeys[_registeredCommandCount++] = nameKey;
        }
        return OK();
    }

    ReturnCode _deregisterCommands() {
        auto &registrar = CommandService::registrar();
        for (const auto &nameKey : _registeredCommandKeys) {
            if (!nameKey) {
                continue;
            }
            FAIL_IF_ERR_FWD(registrar.deregisterCommand(nameKey),
                            "Failed to deregister command with key %s for %s",
                            nameKey, Derived::name);
        }
        return OK();
    }

  private:
    uint8_t _registeredCommandCount = 0;
    std::array<Totem::CommandBackend::detail::Store::CommandNameKey,
               CommandConfig::maxEntriesPerClass>
        _registeredCommandKeys{};
};

template <typename T>
concept HasCommandProvider = requires(T cls) {
    { cls.commands() } -> std::same_as<std::span<CommandDesc *>>;
};

template <class Derived, typename CommandSet> struct CommandsContract {
    static_assert(IsNamedEntity<Derived>, "Derived must be a named entity");
    static_assert(HasCommandProvider<CommandSet>,
                  "CommandSet must provide static commands() method returning "
                  "span of CommandDesc pointers");
};
