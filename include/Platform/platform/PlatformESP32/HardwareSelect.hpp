#pragma once

// IWYU pragma: begin_exports

#if defined(PLATFORM_ESP32_ORIG)
#include "hardware/HardwareESP32Orig.hpp"

#elif defined(PLATFORM_ESP32S3_ZERO)
#include "hardware/HardwareESP32S3Zero.hpp"

#elif defined(PLATFORM_ESP32C3_SUPERMINI)
#include "hardware/HardwareESP32SC3SuperMini.hpp"

#else
#error "No supported platform selected"
#endif

// IWYU pragma: end_exports
