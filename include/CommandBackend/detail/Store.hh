#pragma once

#include "CommandBackend/detail/Directory.hh"
#include "Types/Command.hh"
#include "Types/Error.hh"
#include <expected>

namespace Totem::CommandBackend::detail {

class Store {
  public:
    Store() { _enableRegistration(); }
    ~Store() { _disableRegistration(); }

    using CommandNameKey = Directory::EntryNameKey;

    static constexpr const char *name = "Command::Store";

    std::expected<CommandNameKey, ReturnCode>
    add(const char *commandName, const CommandDesc &commandDesc) {
        return _directory.add(CommandNameKey::fromCharPtr(commandName),
                              commandDesc);
    }

    [[nodiscard]] std::expected<CommandDesc, ReturnCode>
    get(const CommandNameKey &commandNameKey) const {
        return _directory.getCopy(commandNameKey);
    }

  private:
    void _disableRegistration() { _directory.disableRegistration(); }
    void _enableRegistration() { _directory.enableRegistration(); }

    Directory _directory;

    using DefaultError = CoreError;
};

} // namespace Totem::CommandBackend::detail
