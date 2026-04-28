#pragma once

#if defined(PLATFORM_ESP32)

#include "Platform/platform/PlatformESP32/HardwareSelect.hpp" // IWYU pragma: export

#else

#error "No supported platform selected"

#endif
