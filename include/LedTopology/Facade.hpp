#pragma once

#include "LedTopology/Interfaces/Types.hpp" // IWYU pragma: export
#include "LedTopology/detail/DenseUmbrella.hpp"
#include "LedTopology/detail/OwnedPixels.hpp"
#include "LedTopology/detail/Umbrella.hpp"
#include "StaticConfig/LedDisplay.hpp"

namespace Totem::LedTopology {

using Umbrella = detail::Umbrella;
using DenseUmbrella = detail::DenseUmbrella;
using Surface = LedDisplayConfig::Topology;
using OwnedPixels = detail::OwnedPixels;

} // namespace Totem::LedTopology
