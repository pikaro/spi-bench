// IWYU pragma: private
// IWYU pragma: friend "SecretStorage/detail/.*"

#pragma once

#include "Platform/PlatformSelect.hpp"

#if defined(PLATFORM_ESP32)
#include "SecretStorage/detail/platform/PlatformESP32.hpp"
#else
#error "No supported SecretStorage platform selected"
#endif

namespace Totem::SecretStorage::detail {

using Platform = PlatformESP32;

} // namespace Totem::SecretStorage::detail
