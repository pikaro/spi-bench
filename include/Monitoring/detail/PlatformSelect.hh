#pragma once

// IWYU pragma: begin_exports

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hh"
#elif defined(PLATFORM_TEST)
#include "platform/PlatformTest/Facade.hh"
#else
#error "No supported platform selected"
#endif

// IWYU pragma: end_exports

namespace Totem::Monitoring::detail {
using Platform = platform::Platform;
} // namespace Totem::Monitoring::detail
