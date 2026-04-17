#pragma once

#include "PubSubBackend/Interfaces/Envelope.hh"
#include "Types/Error.hh"

namespace Totem::PubSubBackend {

using SubscriberCallback = ReturnCode (*)(void *subscriber,
                                          const Envelope &frameView);
struct Subscriber {
    void *subscriber;
    SubscriberCallback callback;
};

} // namespace Totem::PubSubBackend
