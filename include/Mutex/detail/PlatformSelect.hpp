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

namespace Totem::Mutex::detail {
using Platform = platform::Platform;

// Globals
using MutexHandle = ::platform::MutexHandle;
} // namespace Totem::Mutex::detail
