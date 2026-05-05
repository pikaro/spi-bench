// IWYU pragma: private
// IWYU pragma: friend "LoggingBackend/detail/.*"

#pragma once

// IWYU pragma: begin_exports

#include "Platform/PlatformSelect.hpp"

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hpp"
#else
#error "No supported logging platform selected"
#endif

// IWYU pragma: end_exports

namespace Totem::LoggingBackend::detail {

using Platform = platform::Platform;

} // namespace Totem::LoggingBackend::detail
