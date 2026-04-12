#pragma once

#include "Macros/internal/Markers.hh"
#include "PubSubBackend/Interfaces/Types.hh"
#include <cstdint>

namespace Totem::PubSubBackend {

struct WIRE_MSG Header {
    uint32_t timestampMs{};
    MessageId messageId{};
    TopicId topic{};
    NodeId source{};
    uint16_t payloadSize{};

    bool operator==(const Header &other) const {
        return messageId == other.messageId && source == other.source;
    }
};

} // namespace Totem::PubSubBackend
