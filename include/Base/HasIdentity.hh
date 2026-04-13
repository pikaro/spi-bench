#pragma once

#include <string_view>

template <typename T> consteval std::string_view type_name() {
    std::string_view fun = __PRETTY_FUNCTION__;
    auto start = fun.find("T = ");
    start += 4;
    auto end = fun.find_first_of(";]", start);
    return fun.substr(start, end - start);
}

struct Identity {
    std::string_view name;
};

template <class Derived> class HasIdentity {
  public:
    [[nodiscard]] const Identity &identity() const { return _identity; }
    [[nodiscard]] std::string_view name() const { return _identity.name; }

  protected:
    explicit HasIdentity(std::string_view name = defaultName())
        : _identity{.name = name} {}

  private:
    static consteval std::string_view defaultName() {
        if constexpr (requires { Derived::name; }) {
            return Derived::name;
        } else {
            return type_name<Derived>();
        }
    }

    const Identity _identity;
};
