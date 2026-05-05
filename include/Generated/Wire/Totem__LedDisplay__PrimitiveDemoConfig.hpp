#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Interfaces/Types.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::PrimitiveDemoConfig> {
    using Type = ::Totem::LedDisplay::PrimitiveDemoConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::primitive>{"primitive"},
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::width>{"width"},
        Field<&Type::density>{"density"},
        Field<&Type::speed>{"speed"}
    );
};

} // namespace Totem::Generated::Wire
