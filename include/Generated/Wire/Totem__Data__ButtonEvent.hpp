#pragma once

#include "Generated/Wire/Support.hpp"
#include "Data/ButtonEvent.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::Data::ButtonEvent> {
    using Type = ::Totem::Data::ButtonEvent;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::event>{"event"},
        Field<&Type::button>{"button"}
    );
};

} // namespace Totem::Generated::Wire
