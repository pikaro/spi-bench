#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/Interfaces/ICommandCatalog.hpp"
#include "CommandBackend/Interfaces/Types.hpp"
#include "CommandBackend/detail/Directory.hpp"
#include "CommandBackend/detail/Types.hpp" // IWYU pragma: keep
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <expected>
#include <utility>

namespace Totem::CommandBackend::detail {

class Store : public ICommandCatalog {
  public:
    using CommandKeySnapshot = Totem::CommandBackend::CommandKeySnapshot;

    Store() { _enableRegistration(); }
    ~Store() { _disableRegistration(); }

    static constexpr const char *name = "Command::Store";

    std::expected<CommandNameKey, ReturnCode>
    add(void *ctx, const CommandDesc &commandDesc) {
        return _directory.add(ctx, commandDesc);
    }

    ReturnCode remove(const CommandNameKey &commandNameKey) {
        return _directory.remove(commandNameKey);
    }

    [[nodiscard]] std::expected<std::pair<void *, const CommandDesc *>,
                                ReturnCode>
    get(const CommandNameKey &commandNameKey) const override {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            command, _directory.getCopy(commandNameKey),
            "Failed to get command from directory for command name " SV_FMT,
            SV_ARG(commandNameKey.view()));
        FAIL_IF_NULL(command.desc, std::unexpected(ERR(InvalidData)),
                     "Command directory entry " SV_FMT
                     " has no descriptor",
                     SV_ARG(commandNameKey.view()));
        return std::make_pair(command.ctx, command.desc);
    }

    [[nodiscard]] std::expected<bool, ReturnCode>
    contains(const CommandNameKey &commandNameKey) const override {
        return _directory.contains(commandNameKey);
    }

    [[nodiscard]] std::expected<CommandKeySnapshot, ReturnCode>
    snapshotCommandKeys() const override {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(keys, _directory.snapshotKeys(),
                                          "Failed to snapshot command keys");
        CommandKeySnapshot out{};
        out.count = keys.count;
        for (std::size_t i = 0; i < keys.count; ++i) {
            out.keys[i] = keys.keys[i];
        }
        return out;
    }

  private:
    void _disableRegistration() { _directory.disableRegistration(); }
    void _enableRegistration() { _directory.enableRegistration(); }

    Directory _directory;
};

} // namespace Totem::CommandBackend::detail
