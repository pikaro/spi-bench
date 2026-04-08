#include <cstddef>
#include <cstdint>
#include <type_traits>

template <size_t Max>
using SmallestUintType = std::conditional_t<
    (Max <= UINT8_MAX), uint8_t,
    std::conditional_t<
        (Max <= UINT16_MAX), uint16_t,
        std::conditional_t<(Max <= UINT32_MAX), uint32_t, size_t>>>;
