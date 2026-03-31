#pragma once

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hh"
#elif defined(PLATFORM_TEST)
#include "platform/PlatformTest.hh"
#else
#error "No supported TaskController platform selected"
#endif

namespace Totem::TaskController::detail {
using TaskHandle = ::platform::TaskHandle;
using Platform = platform::Platform;
} // namespace Totem::TaskController::detail
