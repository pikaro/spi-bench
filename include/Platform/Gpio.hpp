#pragma once

// IWYU pragma: begin_exports

#if defined(PLATFORM_ESP32)
#include "Platform/platform/PlatformESP32/Gpio.hpp"

#elif defined(PLATFORM_TEST)
#include "Platform/platform/PlatformTest/Gpio.hpp"

#else
#error "No supported GPIO platform selected"
#endif

// IWYU pragma: end_exports
