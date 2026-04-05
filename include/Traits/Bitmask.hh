#pragma once

#include <type_traits>

template <typename Enum> struct BitmaskTrait : std::false_type {};

template <typename Enum>
    requires BitmaskTrait<Enum>::value
constexpr Enum operator|(Enum a, Enum b) {
    using U = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<U>(a) | static_cast<U>(b));
}

template <typename Enum>
    requires BitmaskTrait<Enum>::value
constexpr Enum operator&(Enum a, Enum b) {
    using U = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<U>(a) & static_cast<U>(b));
}

template <typename Enum>
    requires BitmaskTrait<Enum>::value
constexpr Enum operator~(Enum a) {
    using U = std::underlying_type_t<Enum>;
    return static_cast<Enum>(~static_cast<U>(a));
}

template <typename Enum>
    requires BitmaskTrait<Enum>::value
constexpr Enum &operator|=(Enum &a, Enum b) {
    return a = a | b;
}

template <typename Enum>
    requires BitmaskTrait<Enum>::value
constexpr Enum &operator&=(Enum &a, Enum b) {
    return a = a & b;
}
