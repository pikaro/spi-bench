#pragma once

#include "PubSubBackend/Interfaces/Frame.hh"
#include "Types/Error.hh"
#include <functional>

namespace Totem::PubSubBackend {

using SubscriberCallback =
    std::function<ReturnCode(const PublishRequest &frameView)>;

} // namespace Totem::PubSubBackend
