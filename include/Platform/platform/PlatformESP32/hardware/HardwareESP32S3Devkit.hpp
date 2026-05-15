// IWYU pragma: private

#pragma once

#include "Macros/internal/Hardware.hpp"
#include "Platform/platform/PlatformESP32/hardware/HardwareESP32S3.hpp" // IWYU pragma: keep
#include <cstdint>

namespace platform {

enum class Esp32S3DevkitPin : uint8_t {
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
    GPIO14 = PIN(Esp32S3, GPIO14),
    GPIO15 = PIN(Esp32S3, GPIO15),
    GPIO16 = PIN(Esp32S3, GPIO16),
    GPIO17 = PIN(Esp32S3, GPIO17),
    GPIO18 = PIN(Esp32S3, GPIO18),
    GPIO21 = PIN(Esp32S3, GPIO21),
    GPIO38 = PIN(Esp32S3, GPIO38),
    GPIO39 = PIN(Esp32S3, GPIO39),
    GPIO40 = PIN(Esp32S3, GPIO40),
    GPIO41 = PIN(Esp32S3, GPIO41),
    GPIO42 = PIN(Esp32S3, GPIO42),
    GPIO47 = PIN(Esp32S3, GPIO47),

    USBDMinus = PIN(Esp32S3, USBDMinus),
    USBDPlus = PIN(Esp32S3, USBDPlus),

    PsramGPIO35 = PIN(Esp32S3, PsramGPIO35),
    PsramGPIO36 = PIN(Esp32S3, PsramGPIO36),
    PsramGPIO37 = PIN(Esp32S3, PsramGPIO37),

    StatusLed = PIN(Esp32S3, GPIO48),
};

} // namespace platform
