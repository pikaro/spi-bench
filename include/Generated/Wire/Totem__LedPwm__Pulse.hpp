#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedPwm/Interfaces/Types.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedPwm::Pulse> {
    using Type = ::Totem::LedPwm::Pulse;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::peak>{"peak"},
        Field<&Type::riseMs>{"riseMs"},
        Field<&Type::holdMs>{"holdMs"},
        Field<&Type::fallMs>{"fallMs"},
        Field<&Type::curve>{"curve"}
    );
};

} // namespace Totem::Generated::Wire
