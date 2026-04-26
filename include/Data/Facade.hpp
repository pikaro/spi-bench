#pragma once

// IWYU pragma: begin_exports

#include "Data/Nodes.hpp"
#include "Data/PubSub.hpp"

// IWYU pragma: end_exports

namespace Totem::Data {

template <NodeName N> struct Data {
    using PubSub = PubSub::PubSubData<N>;
};

} // namespace Totem::Data
