#pragma once

#include "Macros/internal/Markers.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include <cstdint>

namespace Totem::PubSubBackend {

struct WIRE_MSG Header {
    uint32_t timestampMs{};
    MessageId messageId{};
    TopicId topic{};
    NodeId source{};
    TrafficClass trafficClass{TrafficClass::Noncritical};
    uint16_t payloadSize{};

    bool operator==(const Header &other) const {
        return messageId == other.messageId && source == other.source;
    }
};

} // namespace Totem::PubSubBackend
