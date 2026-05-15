// IWYU pragma: private

#pragma once

#include "Macros/internal/Hardware.hpp"
#include "Platform/platform/PlatformESP32/hardware/HardwareESP32S3.hpp" // IWYU pragma: keep
#include <cstdint>

namespace platform {

enum class Esp32S3ZeroPin : uint8_t {
    GPIO1 = PIN(Esp32S3, GPIO1),
    GPIO2 = PIN(Esp32S3, GPIO2),
    GPIO4 = PIN(Esp32S3, GPIO4),
    GPIO5 = PIN(Esp32S3, GPIO5),
    GPIO6 = PIN(Esp32S3, GPIO6),
    GPIO7 = PIN(Esp32S3, GPIO7),
    GPIO8 = PIN(Esp32S3, GPIO8),
    GPIO9 = PIN(Esp32S3, GPIO9),
    GPIO10 = PIN(Esp32S3, GPIO10),
    GPIO11 = PIN(Esp32S3, GPIO11),
    GPIO12 = PIN(Esp32S3, GPIO12),
    GPIO13 = PIN(Esp32S3, GPIO13),

    PadGPIO14 = PIN(Esp32S3, GPIO14),
    PadGPIO15 = PIN(Esp32S3, GPIO15),
    PadGPIO16 = PIN(Esp32S3, GPIO16),
    PadGPIO17 = PIN(Esp32S3, GPIO17),
    PadGPIO18 = PIN(Esp32S3, GPIO18),
    PadGPIO38 = PIN(Esp32S3, GPIO38),
    PadGPIO39 = PIN(Esp32S3, GPIO39),
    PadGPIO40 = PIN(Esp32S3, GPIO40),
    PadGPIO41 = PIN(Esp32S3, GPIO41),
    PadGPIO42 = PIN(Esp32S3, GPIO42),

    StatusLed = PIN(Esp32S3, GPIO21),
};

} // namespace platform
