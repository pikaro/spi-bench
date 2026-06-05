#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/RadialCurtain/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::RadialCurtainConfig> {
    using Type = ::Totem::LedDisplay::Animations::RadialCurtainConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::width>{"width"},
        Field<&Type::tilt>{"tilt"},
        Field<&Type::speed>{"speed"},
        Field<&Type::outerOrigin>{"outerOrigin"},
        Field<&Type::spokePhase>{"spokePhase"}
    );
};

} // namespace Totem::Generated::Wire
