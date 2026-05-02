#pragma once

// IWYU pragma: begin_exports

#include "platform/PlatformGeneric.hpp"

#if defined(PLATFORM_ESP32)
#include "platform/PlatformESP32.hpp"
#endif

// IWYU pragma: end_exports

namespace Totem::Platform::Crc {

using PlatformGeneric = ::platform::crc::PlatformGeneric;
using Generic = PlatformGeneric;

#if defined(PLATFORM_ESP32)
using PlatformESP32 = ::platform::crc::PlatformESP32;
using EspIdf = PlatformESP32;
using Platform = PlatformESP32;
#else
using Platform = PlatformGeneric;
#endif

} // namespace Totem::Platform::Crc
