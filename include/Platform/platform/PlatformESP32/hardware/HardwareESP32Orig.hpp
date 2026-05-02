// IWYU pragma: private

#pragma once

#include "soc/gpio_num.h"
#include <cstdint>

namespace platform {

enum class Esp32OrigPin : uint8_t {
    BOOT = GPIO_NUM_0,

    UART0_TX = GPIO_NUM_1,

    StrappingGPIO2 = GPIO_NUM_2,

    UART0_RX = GPIO_NUM_3,

    StrappingGPIO4 = GPIO_NUM_4,
    StrappingGPIO5 = GPIO_NUM_5,

    // Reserved for flash
    // GPIO6 = GPIO_NUM_6,
    // GPIO7 = GPIO_NUM_7,
    // GPIO8 = GPIO_NUM_8,
    // GPIO9 = GPIO_NUM_9,
    // GPIO10 = GPIO_NUM_10,
    // GPIO11 = GPIO_NUM_11,

    StrappingGPIO12 = GPIO_NUM_12,
    GPIO13 = GPIO_NUM_13,
    GPIO14 = GPIO_NUM_14,
    StrappingGPIO15 = GPIO_NUM_15,
    StrappingHSPI_MISO = GPIO_NUM_12,
    HSPI_MOSI = GPIO_NUM_13,
    HSPI_SCLK = GPIO_NUM_14,
    StrappingHSPI_CS = GPIO_NUM_15,

    GPIO16 = GPIO_NUM_16,
    GPIO17 = GPIO_NUM_17,

    GPIO18 = GPIO_NUM_18,
    GPIO19 = GPIO_NUM_19,
    GPIO23 = GPIO_NUM_23,
    VSPI_SCLK = GPIO_NUM_18,
    VSPI_MISO = GPIO_NUM_19,
    VSPI_MOSI = GPIO_NUM_23,
    VSPI_CS = GPIO_NUM_5,

    // Reserved for PSRAM
    // GPIO20 = GPIO_NUM_20,
    // GPIO21 = GPIO_NUM_21,

    GPIO22 = GPIO_NUM_22,

    GPIO25 = GPIO_NUM_25,
    GPIO26 = GPIO_NUM_26,
    GPIO27 = GPIO_NUM_27,

    GPIO32 = GPIO_NUM_32,
    GPIO33 = GPIO_NUM_33,

    InputOnlyGPIO34 = GPIO_NUM_34,
    InputOnlyGPIO35 = GPIO_NUM_35,
    InputOnlyGPIO36 = GPIO_NUM_36,

    // GPIOs 37-39 are input-only
    // GPIO37 = GPIO_NUM_37,
    // GPIO38 = GPIO_NUM_38,

    InputOnlyGPIO39 = GPIO_NUM_39,
};

} // namespace platform
