#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/WheelIndicator/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::WheelIndicatorConfig> {
    using Type = ::Totem::LedDisplay::Animations::WheelIndicatorConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::spokes>{"spokes"},
        Field<&Type::falloff>{"falloff"}
    );
};

} // namespace Totem::Generated::Wire
