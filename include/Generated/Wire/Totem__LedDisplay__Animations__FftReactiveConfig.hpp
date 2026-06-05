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
        Field<&Type::value>{"value"},
        Field<&Type::baseValue>{"baseValue"},
        Field<&Type::radialMode>{"radialMode"},
        Field<&Type::angularMode>{"angularMode"},
        Field<&Type::symmetry>{"symmetry"},
        Field<&Type::contrast>{"contrast"},
        Field<&Type::peakSensitivity>{"peakSensitivity"},
        Field<&Type::flowSpeed>{"flowSpeed"},
        Field<&Type::hueModulation>{"hueModulation"}
    );
};

} // namespace Totem::Generated::Wire
