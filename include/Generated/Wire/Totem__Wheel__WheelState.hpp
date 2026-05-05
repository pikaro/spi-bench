#pragma once

#include "Generated/Wire/Support.hpp"
#include "Wheel/Interfaces/Wire.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::Wheel::WheelState> {
    using Type = ::Totem::Wheel::WheelState;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::position>{"position"},
        Field<&Type::delta>{"delta"}
    );
};

} // namespace Totem::Generated::Wire
