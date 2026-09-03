#pragma once

#include "Generated/Wire/Support.hpp"
#include "Data/MenuEvent.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::Data::MenuEvent> {
    using Type = ::Totem::Data::MenuEvent;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::position>{"position"},
        Field<&Type::event>{"event"},
        Field<&Type::item>{"item"},
        Field<&Type::menu>{"menu"}
    );
};

} // namespace Totem::Generated::Wire
