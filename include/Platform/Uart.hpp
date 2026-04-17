#pragma once

// IWYU pragma: begin_exports

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32/Uart.hh"
#elif defined(PLATFORM_TEST)
#include "platform/PlatformTest/Uart.hh"
#else
#error "No supported platform selected"
#endif

// IWYU pragma: end_exports
