#pragma once

// IWYU pragma: begin_exports

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32/Facade.hh"

#if defined(PLATFORM_ESP32_ORIG)
#include "platform/PlatformESP32Orig.hh"
#endif

#elif defined(PLATFORM_TEST)
#include "platform/PlatformTest/Facade.hh"
#else
#error "No supported platform selected"
#endif

// IWYU pragma: end_exports
