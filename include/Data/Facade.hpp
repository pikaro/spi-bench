#pragma once

// IWYU pragma: begin_exports

#include "Data/ButtonEvent.hpp"
#include "Data/DialEvent.hpp"
#include "Data/MenuEvent.hpp"
#include "Data/Nodes.hpp"
#include "Data/Peripherals.hpp"
#include "Data/PubSub.hpp"

// IWYU pragma: end_exports

namespace Totem::Data {

template <NodeName N> struct Data {
    using PubSub = PubSub::PubSubData<N>;
};

} // namespace Totem::Data
