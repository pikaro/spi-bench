#pragma once

#include "Config.hh"
#include "PubSubBackend/Interfaces/Envelope.hh"
#include "PubSubBackend/Interfaces/Types.hh"
#include "Types/Error.hh"
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

} // namespace Totem::PubSubBackend::detail
