#pragma once

#include "StaticConfig/Command.hpp"
#include "Types/Collection.hpp"
#include <array>
#include <cstddef>

namespace Totem::CommandBackend {

using CommandNameKey = NameKey<CommandConfig::maxNameLength>;

struct CommandKeySnapshot {
    std::array<CommandNameKey, CommandConfig::maxEntries> keys{};
    std::size_t count = 0;
};

} // namespace Totem::CommandBackend
