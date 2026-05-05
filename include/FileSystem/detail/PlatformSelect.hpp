// IWYU pragma: private
// IWYU pragma: friend "FileSystem/detail/.*"

#pragma once

// IWYU pragma: begin_exports

#include "Platform/PlatformSelect.hpp"

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hpp"
#else
#error "No supported FileSystem platform selected"
#endif

// IWYU pragma: end_exports

namespace Totem::FileSystem::detail {

using Platform = platform::Platform;

} // namespace Totem::FileSystem::detail
