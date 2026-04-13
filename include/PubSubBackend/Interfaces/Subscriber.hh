#pragma once

#include "PubSubBackend/Interfaces/Envelope.hh"
#include "Types/Error.hh"

namespace Totem::PubSubBackend {

using SubscriberCallback = ReturnCode (*)(const Envelope &frameView);

} // namespace Totem::PubSubBackend
