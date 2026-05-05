#pragma once

#include <utility>

template <bool Enabled, class T> struct ConditionalMember;

template <class T> struct ConditionalMember<true, T> {
    template <typename... Args>
    explicit ConditionalMember(Args &&...args)
        : value(std::forward<Args>(args)...) {}

    T value;

    T &get() { return value; }
    const T &get() const { return value; }
};

template <class T> struct ConditionalMember<false, T> {
    // no T
    template <typename... Args> explicit ConditionalMember(Args &&... /*args*/) {}

    T &get();
    const T &get() const;
};
