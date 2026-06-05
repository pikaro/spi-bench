#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/Starburst/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::StarburstConfig> {
    using Type = ::Totem::LedDisplay::Animations::StarburstConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::rise>{"rise"},
        Field<&Type::peak>{"peak"},
        Field<&Type::wake>{"wake"},
        Field<&Type::points>{"points"},
        Field<&Type::pointGain>{"pointGain"},
        Field<&Type::twist>{"twist"},
        Field<&Type::cycles>{"cycles"}
    );
};

} // namespace Totem::Generated::Wire
