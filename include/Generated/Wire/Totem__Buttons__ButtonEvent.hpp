#pragma once

#include "Generated/Wire/Support.hpp"
#include "Buttons/Interfaces/Wire.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::Buttons::ButtonEvent> {
    using Type = ::Totem::Buttons::ButtonEvent;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::type>{"type"},
        Field<&Type::button>{"button"}
    );
};

} // namespace Totem::Generated::Wire
