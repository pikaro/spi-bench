#pragma once

#include "Data.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include "Types/Logging.hpp"
#include <cstdint>

namespace Totem::PubSubBackend::detail {

using Spec = NodeData::PubSub;

using MessageId = uint32_t;

using TopicMask = TopicId;
using TransportId = uint8_t;
using TransportMask = TransportId;

struct StoredFrame {
    Envelope envelope{};
    TransportMask pendingMask = 0;
    uint8_t pendingCount = 0;

    [[nodiscard]] bool valid() const {
        return pendingCount > 0 ? pendingMask != 0 : true;
    }
};

using FrameHandle = StoredFrame *;

using PollIntoCallback = ReturnCode (*)(void *ctx, const Envelope &envelope);
using PublishCallback = ReturnCode (*)(void *ctx, const Envelope &envelope);

inline constexpr LogComponent logComponent = LogComponent::PubSub;

} // namespace Totem::PubSubBackend::detail
