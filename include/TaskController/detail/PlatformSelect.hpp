// IWYU pragma: private
// IWYU pragma: friend "TaskController/detail/.*"

#pragma once

// IWYU pragma: begin_exports

#include "Platform/PlatformSelect.hpp"

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hpp"
#elif defined(PLATFORM_TEST)
#include "platform/PlatformTest.hpp"
#else
#error "No supported TaskController platform selected"
#endif

// IWYU pragma: end_exports

namespace Totem::TaskController::detail {
using TaskHandle = ::platform::TaskHandle;
using Platform = platform::Platform;
} // namespace Totem::TaskController::detail
