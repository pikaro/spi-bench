#pragma once

#include "LedTopology/Interfaces/Types.hpp" // IWYU pragma: export
#include "LedTopology/detail/OwnedPixels.hpp"
#include "LedTopology/detail/Umbrella.hpp"

namespace Totem::LedTopology {

using Umbrella = detail::Umbrella;
using OwnedPixels = detail::OwnedPixels;

} // namespace Totem::LedTopology
