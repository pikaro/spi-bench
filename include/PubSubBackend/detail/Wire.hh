#pragma once

#include "Macros/internal/Markers.hh"
#include "PubSubBackend/Interfaces/Types.hh"
#include <cstdint>

namespace Totem::PubSubBackend::detail {

enum class SubscribeEventType : uint8_t {
    Register,
    Unregister,
};

struct WIRE_MSG PubSubEvent {
    TopicId topic;
    SubscribeEventType type;
};

} // namespace Totem::PubSubBackend::detail
