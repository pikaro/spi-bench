#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Directory.hpp"
#include "CommandBackend/detail/Types.hpp" // IWYU pragma: keep
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <expected>
#include <utility>

namespace Totem::CommandBackend::detail {

class Store {
  public:
    Store() { _enableRegistration(); }
    ~Store() { _disableRegistration(); }

    using CommandNameKey = Directory::EntryKey;

    static constexpr const char *name = "Command::Store";

    std::expected<CommandNameKey, ReturnCode>
    add(const char *commandName, void *ctx, const CommandDesc &commandDesc) {
        return _directory.add(CommandNameKey::fromCharPtr(commandName), ctx,
                              commandDesc);
    }

    ReturnCode remove(const CommandNameKey &commandNameKey) {
        return _directory.remove(commandNameKey);
    }

    [[nodiscard]] std::expected<std::pair<void *, const CommandDesc>,
                                ReturnCode>
    get(const CommandNameKey &commandNameKey) const {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            command, _directory.getCopy(commandNameKey),
            "Failed to get command from directory for command name " SV_FMT,
            SV_ARG(commandNameKey.view()));
        return std::make_pair(command.ctx, *command.desc);
    }

  private:
    void _disableRegistration() { _directory.disableRegistration(); }
    void _enableRegistration() { _directory.enableRegistration(); }

    Directory _directory;
};

} // namespace Totem::CommandBackend::detail
