#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/Cymatic/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::CymaticConfig> {
    using Type = ::Totem::LedDisplay::Animations::CymaticConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::sourceMode>{"sourceMode"},
        Field<&Type::wavelength>{"wavelength"},
        Field<&Type::speed>{"speed"},
        Field<&Type::contrast>{"contrast"},
        Field<&Type::hueStep>{"hueStep"}
    );
};

} // namespace Totem::Generated::Wire
