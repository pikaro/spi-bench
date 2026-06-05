#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/Bolt/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::BoltConfig> {
    using Type = ::Totem::LedDisplay::Animations::BoltConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::width>{"width"},
        Field<&Type::jitter>{"jitter"},
        Field<&Type::forks>{"forks"},
        Field<&Type::seed>{"seed"},
        Field<&Type::outerOrigin>{"outerOrigin"}
    );
};

} // namespace Totem::Generated::Wire
