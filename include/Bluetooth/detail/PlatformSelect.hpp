// IWYU pragma: private

#pragma once

#include "sdkconfig.h"

#if defined(PLATFORM_ESP32) && CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
#include "Bluetooth/detail/platform/NimbleCentral.hpp"
namespace Totem::Bluetooth::detail {
using SelectedPlatform = Totem::Bluetooth::detail::platform::NimbleCentral;
} // namespace Totem::Bluetooth::detail
#else
#include "Bluetooth/detail/platform/NullCentral.hpp"
namespace Totem::Bluetooth::detail {
using SelectedPlatform = Totem::Bluetooth::detail::platform::NullCentral;
} // namespace Totem::Bluetooth::detail
#endif
