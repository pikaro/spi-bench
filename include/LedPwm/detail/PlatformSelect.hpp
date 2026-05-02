// IWYU pragma: private
// IWYU pragma: friend "LedPwm/detail/.*"

#pragma once

// IWYU pragma: begin_exports

#include "LedPwm/detail/platform/Config.hpp"
#include "Platform/PlatformSelect.hpp"

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hpp"
#elif defined(PLATFORM_TEST)
#include "platform/PlatformTest.hpp"
#else
#error "No supported platform selected"
#endif

// IWYU pragma: end_exports

namespace Totem::LedPwm::detail {

using Platform = platform::Platform;
using PlatformConfig = platform::Config;

} // namespace Totem::LedPwm::detail
