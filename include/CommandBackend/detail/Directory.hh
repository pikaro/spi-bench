#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hh"
#include "Generic/Directory.hh"
#include "StaticConfig/Command.hh"
#include "Types/Collection.hh"
#include "Types/Error.hh"
#include <cstring>
#include <expected>

namespace Totem::CommandBackend::detail {

using DirectoryImpl = GettableDirectory<NameKey<CommandConfig::maxNameLength>,
                                        CommandDesc, CommandConfig::maxEntries>;

class Directory : public DirectoryImpl {
  public:
    using EntryKey = typename DirectoryImpl::EntryKey;

    explicit Directory() : DirectoryImpl("Command::Directory") {
        _setHooks({
            .self = this,
        });
    }

    std::expected<EntryKey, ReturnCode> add(const EntryKey &commandNameKey,
                                            const CommandDesc &metricDesc) {
        return _addImpl(commandNameKey, metricDesc);
    }
};

} // namespace Totem::CommandBackend::detail
