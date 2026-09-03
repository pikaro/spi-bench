#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/RadialGauge/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::RadialGaugeConfig> {
    using Type = ::Totem::LedDisplay::Animations::RadialGaugeConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::value>{"value"},
        Field<&Type::maximumValue>{"maximumValue"},
        Field<&Type::startHue>{"startHue"},
        Field<&Type::startSaturation>{"startSaturation"},
        Field<&Type::startValue>{"startValue"},
        Field<&Type::endHue>{"endHue"},
        Field<&Type::endSaturation>{"endSaturation"},
        Field<&Type::endValue>{"endValue"},
        Field<&Type::centerRing>{"centerRing"},
        Field<&Type::ringWidth>{"ringWidth"}
    );
};

} // namespace Totem::Generated::Wire
