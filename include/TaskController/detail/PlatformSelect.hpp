#pragma once

#include "Platform/PlatformSelect.hpp" // IWYU pragma: export

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hpp"
#elif defined(PLATFORM_TEST)
#include "platform/PlatformTest.hpp"
#else
#error "No supported TaskController platform selected"
#endif

namespace Totem::TaskController::detail {
using TaskHandle = ::platform::TaskHandle;
using Platform = platform::Platform;
} // namespace Totem::TaskController::detail
