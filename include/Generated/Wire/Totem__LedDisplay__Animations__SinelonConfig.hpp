#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/Sinelon/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::SinelonConfig> {
    using Type = ::Totem::LedDisplay::Animations::SinelonConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::width>{"width"},
        Field<&Type::periodMs>{"periodMs"},
        Field<&Type::outerOrigin>{"outerOrigin"},
        Field<&Type::travelRings>{"travelRings"},
        Field<&Type::bounceAttenuation>{"bounceAttenuation"},
        Field<&Type::spokeGainPct>{"spokeGainPct"},
        Field<&Type::spokeGainPhaseStep>{"spokeGainPhaseStep"}
    );
};

} // namespace Totem::Generated::Wire
