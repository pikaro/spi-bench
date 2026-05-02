#pragma once

// IWYU pragma: begin_exports

#pragma once

#if defined(PLATFORM_ESP32)

#include "sdkconfig.h"

#if CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG ||                             \
    CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "platform/PlatformESP32/UsbSerialConsole.hpp"
#else
#include "platform/PlatformESP32/UartConsole.hpp"
#endif

#else

#error "No supported ESP32 platform selected"

#endif

// IWYU pragma: end_exports
