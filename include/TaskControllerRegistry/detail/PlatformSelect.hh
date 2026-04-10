#pragma once

// IWYU pragma: begin_exports

#include "Platform/PlatformSelect.hh"

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hh"
#elif defined(PLATFORM_TEST)
#include "platform/PlatformTest.hh"
#else
#error "No supported TaskController platform selected"
#endif

// IWYU pragma: end_exports

namespace Totem::TaskControllerRegistry::detail {
using TaskHandle = ::platform::TaskHandle;
using Platform = platform::Platform;
} // namespace Totem::TaskControllerRegistry::detail
