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
    Active,
    Syncing,
    SyncSent,
    SyncReceived,
    Synced,
};

enum class TransceiverState : uint8_t {
    Invalid = 0,
    Initial,
    Sleeping,
    Reading,
    Writing,
};

enum class MessageState : uint8_t {
    Invalid = 0,
    Idle,
    Writing,
    Reading,
};

enum class TransceiverMode : uint8_t {
    ReadWrite,
    WriteRead,
};

} // namespace Totem::Wire::Rs485::detail
