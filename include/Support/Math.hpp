#pragma once

#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace Totem::Support::Math::detail {

template <typename T>
concept integer_not_bool = std::integral<T> && !std::same_as<T, bool>;

template <typename A, typename B>
concept safe_integral_pair =
    integer_not_bool<A> && integer_not_bool<B> &&
    ((std::signed_integral<A> == std::signed_integral<B>) ||
     (std::signed_integral<A> && std::unsigned_integral<B> &&
      std::numeric_limits<B>::digits < std::numeric_limits<A>::digits) ||
     (std::unsigned_integral<A> && std::signed_integral<B> &&
      std::numeric_limits<A>::digits < std::numeric_limits<B>::digits));

template <typename A, typename B> struct safe_integral_result {
  private:
    static constexpr bool same_signedness =
        std::signed_integral<A> == std::signed_integral<B>;

    using Signed = std::conditional_t<std::signed_integral<A>, A, B>;

  public:
    using type =
        std::conditional_t<same_signedness, std::common_type_t<A, B>, Signed>;
};

template <typename A, typename B>
using safe_integral_result_t = typename safe_integral_result<A, B>::type;

template <typename A, typename B>
    requires safe_integral_pair<A, B>
static bool will_overflow_add(A a, B b) {
    using R = safe_integral_result_t<A, B>;

    R x = static_cast<R>(a);
    R y = static_cast<R>(b);

    if constexpr (std::unsigned_integral<R>) {
        return x > std::numeric_limits<R>::max() - y;
    } else {
        if (y > 0) {
            return x > std::numeric_limits<R>::max() - y;
        }
        if (y < 0) {
            return x < std::numeric_limits<R>::min() - y;
        }
        return false;
    }
}

template <typename A, typename B>
    requires safe_integral_pair<A, B>
static bool will_overflow_sub(A a, B b) {
    using R = safe_integral_result_t<A, B>;
    using limits = std::numeric_limits<R>;

    R x = static_cast<R>(a);
    R y = static_cast<R>(b);

    if constexpr (std::unsigned_integral<R>) {
        return x < y;
    } else {
        if (y < 0) {
            return x > limits::max() + y;
        }
        if (y > 0) {
            return x < limits::min() + y;
        }
        return false;
    }
}

template <typename A, typename B>
    requires safe_integral_pair<A, B>
static bool will_overflow_mul(A a, B b) {
    using R = safe_integral_result_t<A, B>;
    using limits = std::numeric_limits<R>;

    R x = static_cast<R>(a);
    R y = static_cast<R>(b);

    if constexpr (std::unsigned_integral<R>) {
        return y != 0 && x > limits::max() / y;
    } else {
        if (x == 0 || y == 0) {
            return false;
        }

        if (x > 0) {
            if (y > 0) {
                return x > limits::max() / y;
            }
            return y < limits::min() / x;
        }
        if (y > 0) {
            return x < limits::min() / y;
        }
        return x < limits::max() / y;
    }
}

template <typename A, typename B>
    requires safe_integral_pair<A, B>
static bool will_overflow_div(A a, B b) {
    using R = safe_integral_result_t<A, B>;
    using limits = std::numeric_limits<R>;

    R x = static_cast<R>(a);
    R y = static_cast<R>(b);

    if (y == 0) {
        return true;
    }
    if constexpr (std::unsigned_integral<R>) {
        return false;
    } else {
        return x == limits::min() && y == R{-1};
    }
}

template <typename A, typename B>
    requires safe_integral_pair<A, B>
static std::optional<safe_integral_result_t<A, B>> safe_add(A a, B b) {
    if (will_overflow_add(a, b)) {
        return std::nullopt;
    }
    using R = safe_integral_result_t<A, B>;
    return static_cast<R>(a) + static_cast<R>(b);
}

template <typename A, typename B>
    requires safe_integral_pair<A, B>
static std::optional<safe_integral_result_t<A, B>> safe_sub(A a, B b) {
    if (will_overflow_sub(a, b)) {
        return std::nullopt;
    }
    using R = safe_integral_result_t<A, B>;
    return static_cast<R>(a) - static_cast<R>(b);
}

template <typename A, typename B>
    requires safe_integral_pair<A, B>
static std::optional<safe_integral_result_t<A, B>> safe_mul(A a, B b) {
    if (will_overflow_mul(a, b)) {
        return std::nullopt;
    }
    using R = safe_integral_result_t<A, B>;
    return static_cast<R>(a) * static_cast<R>(b);
}

template <typename A, typename B>
    requires safe_integral_pair<A, B>
static std::optional<safe_integral_result_t<A, B>> safe_div(A a, B b) {
    using R = safe_integral_result_t<A, B>;
    if (will_overflow_div(a, b)) {
        return std::nullopt;
    }
    return static_cast<R>(a) / static_cast<R>(b);
}

template <typename A, typename B>
    requires safe_integral_pair<A, B>
static std::optional<uint8_t> safe_pct(A a, B b) {
    using R = safe_integral_result_t<A, B>;
    if (will_overflow_mul(a, 100) || will_overflow_div(a, b)) {
        return std::nullopt;
    }
    return static_cast<uint8_t>((static_cast<R>(a) * 100) / static_cast<R>(b));
}

} // namespace Totem::Support::Math::detail

using Totem::Support::Math::detail::will_overflow_add;
using Totem::Support::Math::detail::will_overflow_div;
using Totem::Support::Math::detail::will_overflow_mul;
using Totem::Support::Math::detail::will_overflow_sub;

using Totem::Support::Math::detail::safe_add;
using Totem::Support::Math::detail::safe_div;
using Totem::Support::Math::detail::safe_mul;
using Totem::Support::Math::detail::safe_pct;
using Totem::Support::Math::detail::safe_sub;
