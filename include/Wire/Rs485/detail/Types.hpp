#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include <cstdint>

namespace Totem::Wire::Rs485::detail {

using DefaultError = WireError;
static constexpr LogComponent logComponent = LogComponent::Rs485;

enum class NodeState : uint8_t {
    Initial = 0,
    HelloSent,
    HelloReceived,
    Synced,
};

enum class TransactionKind : uint8_t {
    Data,
    Request,
    Poll,
};

} // namespace Totem::Wire::Rs485::detail
