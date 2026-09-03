#pragma once

#include "Generated/Wire/Support.hpp"
#include "Data/DialEvent.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::Data::DialEvent> {
    using Type = ::Totem::Data::DialEvent;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::position>{"position"},
        Field<&Type::event>{"event"},
        Field<&Type::dial>{"dial"},
        Field<&Type::value>{"value"}
    );
};

} // namespace Totem::Generated::Wire
