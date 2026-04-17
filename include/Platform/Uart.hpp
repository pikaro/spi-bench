#pragma once

// IWYU pragma: begin_exports

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32/Uart.hpp"
#elif defined(PLATFORM_TEST)
#include "platform/PlatformTest/Uart.hpp"
#else
#error "No supported platform selected"
#endif

// IWYU pragma: end_exports
