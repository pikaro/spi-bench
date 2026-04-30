#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/Interfaces/Types.hpp"
#include "CommandBackend/detail/Types.hpp"
#include "Generic/Directory.hpp"
#include "StaticConfig/Command.hpp"
#include "Types/Error.hpp"
#include <cstring>
#include <expected>

namespace Totem::CommandBackend::detail {

struct CommandSlot {
    const CommandDesc *desc;
    void *ctx;
};

class Directory;

using DirectoryImpl =
    BaseGettableDirectory<Directory, CommandNameKey, CommandSlot,
                          CommandConfig::maxEntries>;

class Directory : public DirectoryImpl {
  public:
    static constexpr LogComponent logComponent =
        Totem::CommandBackend::detail::logComponent;

    explicit Directory() : DirectoryImpl("Command::Directory") {}

    std::expected<CommandNameKey, ReturnCode>
    add(void *ctx, const CommandDesc &metricDesc) {
        return _addImpl(CommandNameKey::fromStringView(metricDesc.name),
                        {.desc = &metricDesc, .ctx = ctx});
    }
};

} // namespace Totem::CommandBackend::detail
