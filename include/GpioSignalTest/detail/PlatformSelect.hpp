// IWYU pragma: private
// IWYU pragma: friend "GpioSignalTest/detail/.*"

#pragma once

#if defined(PLATFORM_ESP32)
#include "GpioSignalTest/detail/platform/PlatformESP32.hpp"
#else
#error "No supported GPIO signal test platform selected"
#endif
