#pragma once

#include "soc/gpio_num.h"
#include <cstdint>

namespace platform {

enum class Pin : uint8_t {
    GPIO0 = GPIO_NUM_0,
    GPIO1 = GPIO_NUM_1,
    GPIO3 = GPIO_NUM_3,
    GPIO4 = GPIO_NUM_4,
    GPIO5 = GPIO_NUM_5,
    GPIO6 = GPIO_NUM_6,
    GPIO7 = GPIO_NUM_7,
    GPIO10 = GPIO_NUM_10,
    GPIO20 = GPIO_NUM_20,
    GPIO21 = GPIO_NUM_21,

    A0 = GPIO_NUM_0,
    A1 = GPIO_NUM_1,
    A3 = GPIO_NUM_3,
    A4 = GPIO_NUM_4,
    A5 = GPIO_NUM_5,

    StrappingGPIO2 = GPIO_NUM_2,
    StrappingGPIO8 = GPIO_NUM_8,
    StrappingGPIO9 = GPIO_NUM_9,
};

} // namespace platform
