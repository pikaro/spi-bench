// IWYU pragma: private
// IWYU pragma: friend "Platform/platform/PlatformESP32/.*"

// IWYU pragma: private

#pragma once

// IWYU pragma: begin_exports

#if defined(PLATFORM_ESP32_ORIG)
#include "hardware/HardwareESP32Orig.hpp"
using Pin = platform::Esp32OrigPin;

#elif defined(PLATFORM_ESP32S3_DEVKIT)
#include "hardware/HardwareESP32S3.hpp"
using Pin = platform::Esp32S3Pin;

#elif defined(PLATFORM_ESP32S3_ZERO)
#include "hardware/HardwareESP32S3Zero.hpp"
using Pin = platform::Esp32S3ZeroPin;

#elif defined(PLATFORM_ESP32C3_SUPERMINI)
#include "hardware/HardwareESP32C3SuperMini.hpp"
using Pin = platform::Esp32C3SuperMiniPin;

#else
#error "No supported platform selected"
#endif

// IWYU pragma: end_exports
