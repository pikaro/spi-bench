#pragma once

#include <cstdint>

namespace Totem::PubSubBackend {

using MessageId = uint32_t;
using NodeId = uint8_t;
using PeerId = uint8_t;
using TopicId = uint32_t;
using SubscriberKey = uintptr_t;

enum class TrafficClass : uint8_t {
    Noncritical = 0,
    Critical = 1,
};

} // namespace Totem::PubSubBackend
