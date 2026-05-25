#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/FftReactive/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::FftReactiveConfig> {
    using Type = ::Totem::LedDisplay::Animations::FftReactiveConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::baseHue>{"baseHue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::valueScale>{"valueScale"}
    );
};

} // namespace Totem::Generated::Wire
