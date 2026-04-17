#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Types.hpp"
#include "Generic/Directory.hpp"
#include "StaticConfig/Command.hpp"
#include "Types/Collection.hpp"
#include "Types/Error.hpp"
#include <cstring>
#include <expected>

namespace Totem::CommandBackend::detail {

struct CommandSlot {
    const CommandDesc *desc;
    void *ctx;
};

using DirectoryImpl = GettableDirectory<NameKey<CommandConfig::maxNameLength>,
                                        CommandSlot, CommandConfig::maxEntries>;

class Directory : public DirectoryImpl {
  public:
    using EntryKey = typename DirectoryImpl::EntryKey;

    explicit Directory()
        : DirectoryImpl("Command::Directory",
                        Totem::CommandBackend::detail::logComponent) {
        _setHooks({
            .self = this,
        });
    }

    std::expected<EntryKey, ReturnCode> add(const EntryKey &commandNameKey,
                                            void *ctx,
                                            const CommandDesc &metricDesc) {
        return _addImpl(commandNameKey, {.desc = &metricDesc, .ctx = ctx});
    }
};

} // namespace Totem::CommandBackend::detail
