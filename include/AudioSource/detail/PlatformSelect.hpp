// IWYU pragma: private
// IWYU pragma: friend "AudioSource/detail/.*"

#pragma once

// IWYU pragma: begin_exports

#include "Platform/PlatformSelect.hpp"

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hpp"
#else
#error "No supported audio platform selected"
#endif

// IWYU pragma: end_exports

namespace Totem::AudioSource::detail {

using Platform = platform::Platform;

} // namespace Totem::AudioSource::detail
