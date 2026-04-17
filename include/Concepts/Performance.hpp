#pragma once

#include <cstddef>
#include <functional>
#include <type_traits>

namespace Totem::Concepts {

template <class T> using decay_t = std::decay_t<T>;
template <class T> struct is_refwrap : std::false_type {};
template <class U>
struct is_refwrap<std::reference_wrapper<U>> : std::true_type {};
template <class T>
inline constexpr bool is_refwrap_v = is_refwrap<decay_t<T>>::value;

} // namespace Totem::Concepts

template <class T>
concept IsTriviallyCopyableValue =
    std::is_trivially_copyable_v<Totem::Concepts::decay_t<T>> &&
    std::is_trivially_destructible_v<Totem::Concepts::decay_t<T>>;

template <class T, std::size_t MaxSize = 8>
concept IsTinyValue = sizeof(Totem::Concepts::decay_t<T>) <= MaxSize;

template <class T>
concept IsDefaultConstructibleValue =
    std::is_default_constructible_v<Totem::Concepts::decay_t<T>>;

template <class T, std::size_t MaxSize = 8>
concept IsTinyTrivialValue =
    IsTinyValue<T, MaxSize> && IsTriviallyCopyableValue<T>;

template <class T>
concept IsRefWrap = Totem::Concepts::is_refwrap_v<T>;
