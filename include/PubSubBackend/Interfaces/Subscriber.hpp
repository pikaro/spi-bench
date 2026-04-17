#pragma once

#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "Types/Error.hpp"

namespace Totem::PubSubBackend {

using SubscriberCallback = ReturnCode (*)(void *subscriber,
                                          const Envelope &frameView);
struct Subscriber {
    void *subscriber;
    SubscriberCallback callback;
};

} // namespace Totem::PubSubBackend
