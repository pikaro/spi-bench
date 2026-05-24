#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/SpokeSweep.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <>
struct FieldList<::Totem::LedDisplay::Animations::SpokeSweepConfig> {
    using Type = ::Totem::LedDisplay::Animations::SpokeSweepConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::baseHue>{"baseHue"},
        Field<&Type::hueStride>{"hueStride"},
        Field<&Type::value>{"value"},
        Field<&Type::trailSpokes>{"trailSpokes"},
        Field<&Type::cycles>{"cycles"},
        Field<&Type::markerValue>{"markerValue"}
    );
};

} // namespace Totem::Generated::Wire
