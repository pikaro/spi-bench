#pragma once

// IWYU pragma: begin_exports

#pragma once

#if defined(PLATFORM_ESP32_ORIG)

#include "platform/PlatformESP32/UartConsole.hpp"

#elif defined(PLATFORM_ESP32S3) || defined(PLATFORM_ESP32C3)

#include "platform/PlatformESP32/UsbSerialConsole.hpp"

#else

#error "No supported ESP32 platform selected"

#endif

// IWYU pragma: end_exports
