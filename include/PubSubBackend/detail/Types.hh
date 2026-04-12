#pragma once

#include "Config.hh"
#include "PubSubBackend/Interfaces/Frame.hh"
#include "Types/Error.hh"
#include <cstdint>

namespace Totem::PubSubBackend::detail {

using Spec = NodeData::PubSub;

using MessageId = uint32_t;
using NextMessageIdCallback = MessageId (*)(void *owner);

using TopicMask = TopicId;
using TransportId = uint8_t;
using TransportMask = TransportId;

struct StoredFrame {
    PublishRequest request{};
    TransportMask pendingMask = 0;
    uint8_t pendingCount = 0;

    [[nodiscard]] bool valid() const {
        return pendingCount > 0 ? pendingMask != 0 : true;
    }
};

using FrameHandle = StoredFrame *;

using PollIntoCallback = ReturnCode (*)(void *ctx,
                                        const PublishRequest &request);
using PublishCallback = ReturnCode (*)(void *ctx,
                                       const PublishRequest &request);

} // namespace Totem::PubSubBackend::detail
