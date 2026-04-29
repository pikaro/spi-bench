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

enum class TransceiverMode : uint8_t {
    ReadWrite,
    WriteRead,
};

enum class TransceiverState : uint8_t {
    Invalid = 0,
    Initial,
    WriteRequest,
    ReadReaction,
    ReadRequest,
    WriteReaction,
};

enum class TransceiverEvent : uint8_t {
    Default = 0,
};

enum class FrameTurn : uint8_t {
    Initiated,
    Reaction,
};

enum class TransactionKind : uint8_t {
    Data,
    Request,
    Poll,
};

} // namespace Totem::Wire::Rs485::detail
