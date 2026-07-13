#pragma once

#if defined(PLATFORM_ESP32)
#include "AudioAfe/detail/platform/PlatformESP32.hpp"

namespace Totem::AudioAfe::detail {
using Platform = platform::PlatformESP32;
} // namespace Totem::AudioAfe::detail
#else
#error "AudioAfe currently requires ESP-SR on ESP32"
#endif
