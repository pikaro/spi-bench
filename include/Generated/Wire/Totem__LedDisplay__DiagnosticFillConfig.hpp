#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/DiagnosticFill.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <>
struct FieldList<::Totem::LedDisplay::Animations::DiagnosticFillConfig> {
    using Type = ::Totem::LedDisplay::Animations::DiagnosticFillConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"}
    );
};

} // namespace Totem::Generated::Wire
