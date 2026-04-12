#pragma once

#include <magic_enum/magic_enum.hpp>
#include <type_traits>

template <typename U> constexpr bool is_power_of_two(U x) {
    return x > 0 && (x & (x - 1)) == 0;
}

template <typename E> constexpr bool has_only_bit_flags() {
    static_assert(std::is_enum_v<E>,
                  "has_only_bit_flags requires an enum type");

    for (E enum_ : magic_enum::enum_values<E>()) {
        using U = std::make_unsigned_t<std::underlying_type_t<E>>;
        const U v = static_cast<U>(magic_enum::enum_integer(enum_));

        // Allow 0 for "None", require every other named value to be one bit.
        if (v != 0 && !is_power_of_two(v)) {
            return false;
        }
    }
    return true;
}

template <typename E>
concept IsBitmaskEnum =
    std::is_enum_v<E> && std::is_unsigned_v<std::underlying_type_t<E>> &&
    has_only_bit_flags<E>();

template <typename Enum>
    requires IsBitmaskEnum<Enum>
struct BitmaskTrait : std::false_type {};

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
