#pragma once

#include "Data/Nodes.hpp" // IWYU pragma: export
#include "Data/PubSub.hpp"

namespace Totem::Data {

template <NodeName N> struct Data {
    using PubSub = PubSub::PubSubData<N>;
};

} // namespace Totem::Data
