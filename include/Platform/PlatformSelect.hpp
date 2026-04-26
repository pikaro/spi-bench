#pragma once

// IWYU pragma: begin_exports

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32/Facade.hpp"

#elif defined(PLATFORM_TEST)
#include "platform/PlatformTest/Facade.hpp"

#else
#error "No supported platform selected"
#endif

// IWYU pragma: end_exports
