#pragma once

// IWYU pragma: begin_exports

#if defined(PLATFORM_ESP32)

#include "Platform/platform/PlatformESP32/HardwareSelect.hpp"

#else

#error "No supported platform selected"

#endif

// IWYU pragma: end_exports
