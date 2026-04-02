#pragma once

#include "Generic/Directory.hh"
#include "StaticConfig/Command.hh"
#include "Types/Command.hh"
#include "Types/Error.hh"
#include <cstring>
#include <expected>

namespace Totem::CommandBackend::detail {

using DirectoryImpl =
    Generic::GettableDirectory<CommandDesc, CommandConfig::maxEntries,
                               CommandConfig::maxNameLength>;

class Directory : public DirectoryImpl {
  public:
    explicit Directory() : DirectoryImpl("Command::Directory") {
        _setHooks({
            .self = this,
        });
    }

    std::expected<EntryNameKey, ReturnCode>
    add(const EntryNameKey &commandNameKey, const CommandDesc &metricDesc) {
        return _addImpl(commandNameKey, metricDesc);
    }
};

} // namespace Totem::CommandBackend::detail
