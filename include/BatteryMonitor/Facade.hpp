#pragma once

#include "BatteryMonitor/Interfaces/Config.hpp"      // IWYU pragma: export
#include "BatteryMonitor/Interfaces/Measurement.hpp" // IWYU pragma: export
#include "BatteryMonitor/Interfaces/Types.hpp"       // IWYU pragma: export
#include "BatteryMonitor/detail/BatteryMonitor.hpp"

namespace Totem::BatteryMonitor {

using detail::BatteryMonitor;

} // namespace Totem::BatteryMonitor
