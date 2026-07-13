// IWYU pragma: private
// IWYU pragma: friend "AudioSink/detail/.*"

#pragma once

// IWYU pragma: begin_exports

#include "Platform/PlatformSelect.hpp"

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hpp"
#else
#error "No supported audio sink platform selected"
#endif

// IWYU pragma: end_exports

namespace Totem::AudioSink::detail {

using Platform = platform::Platform;

} // namespace Totem::AudioSink::detail
