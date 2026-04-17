#pragma once

#include "Macros/internal/Markers.hh"
#include <array>
#include <cstddef>
#include <cstdint>

struct WIRE_MSG Message {
    bool flag;
    int intVal;
    uint32_t uint32Val;
    uint16_t uint16Val;
    uint8_t uint8Val;
    std::array<char, 32> strVal;
    std::array<std::byte, 16> byteArrayVal;
};
