#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/Vortex/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::VortexConfig> {
    using Type = ::Totem::LedDisplay::Animations::VortexConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::arms>{"arms"},
        Field<&Type::twist>{"twist"},
        Field<&Type::width>{"width"},
        Field<&Type::cycles>{"cycles"},
        Field<&Type::hueStep>{"hueStep"}
    );
};

} // namespace Totem::Generated::Wire
