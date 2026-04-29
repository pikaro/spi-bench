// IWYU pragma: private

#pragma once

#define BIT_MASK(pos) static_cast<uint8_t>(1U << static_cast<uint8_t>(pos))
#define bit_fmt "%c%c%c%c%c%c%c%c"
#define bits(byte)                                                             \
    ((byte) & 0x80 ? '1' : '0'), ((byte) & 0x40 ? '1' : '0'),                  \
        ((byte) & 0x20 ? '1' : '0'), ((byte) & 0x10 ? '1' : '0'),              \
        ((byte) & 0x08 ? '1' : '0'), ((byte) & 0x04 ? '1' : '0'),              \
        ((byte) & 0x02 ? '1' : '0'), ((byte) & 0x01 ? '1' : '0')
