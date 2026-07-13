#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include <expected>
#include <utility>

namespace Totem::CommandBackend::detail {

struct ICommandCatalog {
    virtual ~ICommandCatalog() = default;

    [[nodiscard]] virtual std::expected<std::pair<void *, const CommandDesc *>,
                                        ReturnCode>
    get(const CommandNameKey &commandNameKey) const = 0;

    [[nodiscard]] virtual std::expected<bool, ReturnCode>
    contains(const CommandNameKey &commandNameKey) const = 0;

    [[nodiscard]] virtual std::expected<CommandKeySnapshot, ReturnCode>
    snapshotCommandKeys() const = 0;
};

} // namespace Totem::CommandBackend::detail
