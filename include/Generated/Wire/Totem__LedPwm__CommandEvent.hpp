#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedPwm/Interfaces/CommandEvent.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedPwm::CommandEvent> {
    using Type = ::Totem::LedPwm::CommandEvent;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::led>{"led"},
        Field<&Type::type>{"type"},
        Field<&Type::brightness>{"brightness"},
        Field<&Type::pulse>{"pulse"},
        Field<&Type::glitter>{"glitter"}
    );
};

} // namespace Totem::Generated::Wire
