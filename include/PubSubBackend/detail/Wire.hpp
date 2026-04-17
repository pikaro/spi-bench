#pragma once

#include "Macros/internal/Markers.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
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
