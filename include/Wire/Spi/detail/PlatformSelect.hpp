#pragma once

// IWYU pragma: begin_exports

#include "Platform/PlatformSelect.hpp"

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hpp"
#elif defined(PLATFORM_TEST)
#include "platform/PlatformTest.hpp"
#else
#error "No supported platform selected"
#endif

// IWYU pragma: end_exports

namespace Totem::Wire::Spi::detail {
// Globals
using Platform = platform::Platform;
} // namespace Totem::Wire::Spi::detail
