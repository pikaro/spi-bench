#pragma once

// IWYU pragma: begin_exports

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hh"
#elif defined(PLATFORM_TEST)
#include "platform/PlatformTest.hh"
#else
#error "No supported platform selected"
#endif

// IWYU pragma: end_exports

namespace Totem::Output::detail {
using Platform = platform::Platform;

// Globals
using RingBufferHandle = ::platform::RingBufferHandle;
} // namespace Totem::Output::detail
