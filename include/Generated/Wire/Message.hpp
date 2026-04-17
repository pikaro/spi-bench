#pragma once

#include "Generated/Wire/Support.hpp"
#include "TestMessage.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Message> {
    using Type = ::Message;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::flag>{"flag"},
        Field<&Type::intVal>{"intVal"},
        Field<&Type::uint32Val>{"uint32Val"},
        Field<&Type::uint16Val>{"uint16Val"},
        Field<&Type::uint8Val>{"uint8Val"},
        Field<&Type::strVal>{"strVal"},
        Field<&Type::byteArrayVal>{"byteArrayVal"}
    );
};

} // namespace Totem::Generated::Wire
