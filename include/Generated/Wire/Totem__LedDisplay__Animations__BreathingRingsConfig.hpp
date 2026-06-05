#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/BreathingRings/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::BreathingRingsConfig> {
    using Type = ::Totem::LedDisplay::Animations::BreathingRingsConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::spacing>{"spacing"},
        Field<&Type::width>{"width"},
        Field<&Type::cycles>{"cycles"},
        Field<&Type::direction>{"direction"},
        Field<&Type::hueStep>{"hueStep"}
    );
};

} // namespace Totem::Generated::Wire
