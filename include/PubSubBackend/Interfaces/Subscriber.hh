#pragma once

#include "PubSubBackend/Interfaces/Envelope.hh"
#include "Types/Error.hh"
#include <functional>

namespace Totem::PubSubBackend {

using SubscriberCallback = std::function<ReturnCode(const Envelope &frameView)>;

} // namespace Totem::PubSubBackend
