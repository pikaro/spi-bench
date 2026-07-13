// IWYU pragma: private

#pragma once

#include "soc/gpio_num.h"
#include <cstdint>

namespace platform {

enum class Esp32S3Pin : uint8_t {
    StrappingGPIO0 = GPIO_NUM_0,

    GPIO1 = GPIO_NUM_1,
    GPIO2 = GPIO_NUM_2,

    StrappingGPIO3 = GPIO_NUM_3,

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
    GPIO14 = GPIO_NUM_14,
    GPIO15 = GPIO_NUM_15,
    GPIO16 = GPIO_NUM_16,
    GPIO17 = GPIO_NUM_17,
    GPIO18 = GPIO_NUM_18,

    USBDMinus = GPIO_NUM_19,
    USBDPlus = GPIO_NUM_20,

    GPIO21 = GPIO_NUM_21,

    PsramGPIO35 = GPIO_NUM_35,
    PsramGPIO36 = GPIO_NUM_36,
    PsramGPIO37 = GPIO_NUM_37,

    GPIO38 = GPIO_NUM_38,
    GPIO39 = GPIO_NUM_39,
    GPIO40 = GPIO_NUM_40,
    GPIO41 = GPIO_NUM_41,
    GPIO42 = GPIO_NUM_42,
    GPIO43 = GPIO_NUM_43,
    GPIO44 = GPIO_NUM_44,

    StrappingGPIO45 = GPIO_NUM_45,
    StrappingGPIO46 = GPIO_NUM_46,

    GPIO47 = GPIO_NUM_47,
    GPIO48 = GPIO_NUM_48,
};

} // namespace platform
