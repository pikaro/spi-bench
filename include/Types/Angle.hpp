#pragma once

#include <cmath>
#include <limits>
#include <type_traits>

template <typename T>
    requires std::is_integral_v<T> && std::is_unsigned_v<T>
struct Angle {
    T value{};

    static constexpr float twoPi = 6.28318530717958647692F;
    static constexpr float modulus =
        static_cast<float>(std::numeric_limits<T>::max()) + 1.0F;

    [[nodiscard]] constexpr Angle operator+(Angle other) const {
        return Angle{static_cast<T>(value + other.value)};
    }

    [[nodiscard]] constexpr Angle operator-(Angle other) const {
        return Angle{static_cast<T>(value - other.value)};
    }

    constexpr Angle &operator+=(Angle other) {
        value = static_cast<T>(value + other.value);
        return *this;
    }

    [[nodiscard]] constexpr float turns() const {
        return static_cast<float>(value) / modulus;
    }

    [[nodiscard]] constexpr float rad() const { return turns() * twoPi; }

    [[nodiscard]] constexpr float deg() const { return turns() * 360.0F; }

    static Angle fromRaw(T raw) { return Angle{raw}; }

    static Angle fromTurns(float turns) {
        turns = turns - std::floor(turns);

        auto raw = static_cast<unsigned long>(std::lround(turns * modulus));

        return Angle{static_cast<T>(
            raw & static_cast<unsigned long>(std::numeric_limits<T>::max()))};
    }

    static Angle fromRad(float radians) { return fromTurns(radians / twoPi); }

    static Angle fromDeg(float degrees) { return fromTurns(degrees / 360.0F); }
};
