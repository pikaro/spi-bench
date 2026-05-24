// IWYU pragma: private

#pragma once

#if defined(PLATFORM_ESP32)
#include "Wifi/detail/platform/PlatformESP32.hpp"
#else
#error "No supported WiFi platform selected"
#endif

namespace Totem::Wifi::detail {

using SelectedPlatform = platform::PlatformESP32;

} // namespace Totem::Wifi::detail
