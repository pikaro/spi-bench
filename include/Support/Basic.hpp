#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

[[nodiscard]] inline size_t hash(const char *str, size_t len) {
    // 64-bit FNV-1a
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint8_t>(str[i]);
        hash *= 1099511628211ULL;
    }
    return static_cast<size_t>(hash);
}

template <std::floating_point T = float, typename Dividend, typename Divisor>
    requires(std::is_arithmetic_v<Dividend> && std::is_arithmetic_v<Divisor>)
inline T safe_pct(Dividend dividend, Divisor divisor) {
    if (divisor == 0) {
        return static_cast<T>(std::numeric_limits<T>::infinity());
    }
    return static_cast<T>(dividend) / static_cast<T>(divisor) *
           static_cast<T>(100);
}

template <typename EnumT, typename R = std::underlying_type_t<EnumT>>
    requires std::is_enum_v<EnumT>
constexpr R to_bits(EnumT value) {
    return static_cast<R>(value);
}

template <typename Enum, typename T = std::underlying_type_t<Enum>>
constexpr bool has_flag(T value, Enum flag) {
    return (T(value) & static_cast<T>(flag)) != 0;
}

[[nodiscard]] constexpr std::size_t bounded_strlen(const char *str,
                                                   std::size_t max) noexcept {
    std::size_t i = 0;
    while (i < max && str[i] != '\0') {
        ++i;
    }
    return i;
}
