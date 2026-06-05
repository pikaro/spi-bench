#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/Lighthouse/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::LighthouseConfig> {
    using Type = ::Totem::LedDisplay::Animations::LighthouseConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::beamWidth>{"beamWidth"},
        Field<&Type::trailSpokes>{"trailSpokes"},
        Field<&Type::cycles>{"cycles"},
        Field<&Type::innerRing>{"innerRing"},
        Field<&Type::outerRing>{"outerRing"}
    );
};

} // namespace Totem::Generated::Wire
