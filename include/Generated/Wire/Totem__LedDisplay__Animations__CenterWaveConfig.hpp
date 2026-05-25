#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/CenterWave/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::CenterWaveConfig> {
    using Type = ::Totem::LedDisplay::Animations::CenterWaveConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::rise>{"rise"},
        Field<&Type::peak>{"peak"},
        Field<&Type::wake>{"wake"}
    );
};

} // namespace Totem::Generated::Wire
