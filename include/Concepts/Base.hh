#pragma once

#include "Types/Error.hh"
#include <concepts>

template <class T>
concept IsNamedEntity = requires {
    { T::name } -> std::same_as<const char *const &>;
} && requires { []() consteval { (void)T::name; }(); };

template <class T>
concept IsBeginnable = requires(T &obj) {
    { obj._onBegin() } -> std::convertible_to<ReturnCode>;
};

template <class T>
concept IsEndable = requires(T &obj) {
    { obj._onEnd() } -> std::convertible_to<ReturnCode>;
};
