#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/OrbitRing/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::OrbitRingConfig> {
    using Type = ::Totem::LedDisplay::Animations::OrbitRingConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::radius>{"radius"},
        Field<&Type::radialWidth>{"radialWidth"},
        Field<&Type::angularWidth>{"angularWidth"},
        Field<&Type::comets>{"comets"},
        Field<&Type::laps>{"laps"},
        Field<&Type::trail>{"trail"}
    );
};

} // namespace Totem::Generated::Wire
