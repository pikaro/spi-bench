#pragma once

#include "Data/Nodes.hh" // IWYU pragma: export
#include "Data/PubSub.hh"

namespace Totem::Data {

template <NodeName N> struct Data {
    using PubSub = PubSub::PubSubData<N>;
};

} // namespace Totem::Data
