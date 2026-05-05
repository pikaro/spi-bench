// IWYU pragma: private
// IWYU pragma: friend "Wire/I2C/detail/.*"

#pragma once

// IWYU pragma: begin_exports

#include "Platform/PlatformSelect.hpp"

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hpp"
#else
#error "No supported I2C platform selected"
#endif

// IWYU pragma: end_exports

namespace Totem::Wire::I2C::detail {

using Platform = platform::Platform;

} // namespace Totem::Wire::I2C::detail
