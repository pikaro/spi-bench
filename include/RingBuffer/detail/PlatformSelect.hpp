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

namespace Totem::RingBuffer::detail {
// Globals
using RingBuffer = platform::RingBuffer;
using RingBufferHandle = ::platform::RingBufferHandle;
} // namespace Totem::RingBuffer::detail
