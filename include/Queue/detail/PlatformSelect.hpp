#pragma once

// IWYU pragma: begin_exports

#include "Platform/PlatformSelect.hh"

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hh"
#elif defined(PLATFORM_TEST)
#include "platform/PlatformTest.hh"
#else
#error "No supported platform selected"
#endif

// IWYU pragma: end_exports

namespace Totem::Queue::detail {

using Platform = platform::Platform;
using QueueHandle = ::platform::QueueHandle;

} // namespace Totem::Queue::detail
