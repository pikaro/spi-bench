// IWYU pragma: private

#pragma once

#include "soc/gpio_num.h"
#include <cstdint>

namespace platform {

enum class Pin : uint8_t {
    GPIO1 = GPIO_NUM_1,
    GPIO2 = GPIO_NUM_2,
    GPIO4 = GPIO_NUM_4,
    GPIO5 = GPIO_NUM_5,
    GPIO6 = GPIO_NUM_6,
    GPIO7 = GPIO_NUM_7,
    GPIO8 = GPIO_NUM_8,
    GPIO9 = GPIO_NUM_9,
    GPIO10 = GPIO_NUM_10,
    GPIO11 = GPIO_NUM_11,
    GPIO12 = GPIO_NUM_12,
    GPIO13 = GPIO_NUM_13,

    PadGPIO14 = GPIO_NUM_14,
    PadGPIO15 = GPIO_NUM_15,
    PadGPIO16 = GPIO_NUM_16,
    PadGPIO17 = GPIO_NUM_17,
    PadGPIO18 = GPIO_NUM_18,
    PadGPIO38 = GPIO_NUM_38,
    PadGPIO39 = GPIO_NUM_39,
    PadGPIO40 = GPIO_NUM_40,
    PadGPIO41 = GPIO_NUM_41,
    PadGPIO42 = GPIO_NUM_42,

    StrappingGPIO3 = GPIO_NUM_3,
    StrappingPadGPIO45 = GPIO_NUM_45,
};

} // namespace platform
