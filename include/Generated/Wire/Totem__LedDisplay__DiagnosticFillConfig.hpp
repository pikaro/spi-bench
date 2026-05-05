#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Interfaces/Types.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::DiagnosticFillConfig> {
    using Type = ::Totem::LedDisplay::DiagnosticFillConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"}
    );
};

} // namespace Totem::Generated::Wire
