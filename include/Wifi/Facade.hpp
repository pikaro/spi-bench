#pragma once

#include "Wifi/Interfaces/Config.hpp" // IWYU pragma: export
#include "Wifi/Interfaces/Types.hpp"  // IWYU pragma: export
#include "Wifi/detail/Commands.hpp"
#include "Wifi/detail/Wifi.hpp"

namespace Totem::Wifi {

using detail::Wifi;

namespace Commands = detail;

} // namespace Totem::Wifi
