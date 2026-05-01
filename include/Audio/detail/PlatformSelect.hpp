// IWYU pragma: private
// IWYU pragma: friend "Audio/detail/.*"

#pragma once

// IWYU pragma: begin_exports

#include "Platform/PlatformSelect.hpp"

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hpp"
#else
#error "No supported audio platform selected"
#endif

// IWYU pragma: end_exports

namespace Totem::Audio::detail {

using Platform = platform::Platform;

} // namespace Totem::Audio::detail
