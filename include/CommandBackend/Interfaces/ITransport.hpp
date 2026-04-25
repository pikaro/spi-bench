#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "Types/Error.hpp"
#include <expected>
#include <string_view>

namespace Totem::CommandBackend::detail {

struct ITransport {
    using PollReturn = std::expected<CommandDesc::Tokens, ReturnCode>;

    virtual ~ITransport() = default;

    [[nodiscard]] virtual std::expected<CommandDesc::Tokens, ReturnCode>
    poll() = 0;
    [[nodiscard]] virtual std::string_view displayName() const = 0;
};

} // namespace Totem::CommandBackend::detail
