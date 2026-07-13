// IWYU pragma: private

#pragma once

#include "Macros/internal/Hardware.hpp"
#include "Platform/platform/PlatformESP32/hardware/HardwareESP32S3.hpp" // IWYU pragma: keep
#include <cstdint>

namespace platform {

enum class Esp32S3NanoPin : uint8_t {
    GPIO1 = PIN(Esp32S3, GPIO1),
    GPIO2 = PIN(Esp32S3, GPIO2),
    GPIO3 = PIN(Esp32S3, StrappingGPIO3),
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
    GPIO17 = PIN(Esp32S3, GPIO17),
    GPIO18 = PIN(Esp32S3, GPIO18),
    GPIO21 = PIN(Esp32S3, GPIO21),
    GPIO38 = PIN(Esp32S3, GPIO38),
    GPIO43 = PIN(Esp32S3, GPIO43),
    GPIO44 = PIN(Esp32S3, GPIO44),
    GPIO47 = PIN(Esp32S3, GPIO47),
    GPIO48 = PIN(Esp32S3, GPIO48),

    A0 = PIN(Esp32S3, GPIO1),
    A1 = PIN(Esp32S3, GPIO2),
    A2 = PIN(Esp32S3, StrappingGPIO3),
    A3 = PIN(Esp32S3, GPIO4),
    A4 = PIN(Esp32S3, GPIO11),
    A5 = PIN(Esp32S3, GPIO12),
    A6 = PIN(Esp32S3, GPIO13),
    A7 = PIN(Esp32S3, GPIO14),
    D0 = PIN(Esp32S3, GPIO44),
    D1 = PIN(Esp32S3, GPIO43),
    D10 = PIN(Esp32S3, GPIO21),
    D11 = PIN(Esp32S3, GPIO38),
    D12 = PIN(Esp32S3, GPIO47),
    D13 = PIN(Esp32S3, GPIO48),
    D2 = PIN(Esp32S3, GPIO5),
    D3 = PIN(Esp32S3, GPIO6),
    D4 = PIN(Esp32S3, GPIO7),
    D5 = PIN(Esp32S3, GPIO8),
    D6 = PIN(Esp32S3, GPIO9),
    D7 = PIN(Esp32S3, GPIO10),
    D8 = PIN(Esp32S3, GPIO17),
    D9 = PIN(Esp32S3, GPIO18),
    LED_BLUE = PIN(Esp32S3, StrappingGPIO45),
    LED_GREEN = PIN(Esp32S3, StrappingGPIO0),
    LED_RED = PIN(Esp32S3, StrappingGPIO46),
};

} // namespace platform
