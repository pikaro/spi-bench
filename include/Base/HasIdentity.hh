#pragma once

#include <cstdio>
#include <functional>
#include <optional>
#include <string_view>

template <typename T> consteval std::string_view type_name() {
    std::string_view fun = __PRETTY_FUNCTION__;
    auto start = fun.find("T = ");
    start += 4;
    auto end = fun.find_first_of(";]", start);
    return fun.substr(start, end - start);
}

struct InstanceIdentity {
    std::string_view name;
    InstanceIdentity *owner = nullptr;

    template <class T>
    static consteval InstanceIdentity
    create(std::string_view name,
           std::optional<std::reference_wrapper<InstanceIdentity>> owner =
               std::nullopt) {
        InstanceIdentity ret{
            .name = name,
            .owner = owner ? &owner->get() : nullptr,
        };

        return ret;
    }
};

template <class Derived> class HasIdentity {
  public:
    [[nodiscard]] const InstanceIdentity &identity() const { return _identity; }
    [[nodiscard]] std::string_view name() const { return _identity.name; }

  protected:
    explicit HasIdentity(std::string_view name = type_name<Derived>())
        : _identity(InstanceIdentity::create<Derived>(name)) {}

  private:
    const InstanceIdentity _identity;
};
