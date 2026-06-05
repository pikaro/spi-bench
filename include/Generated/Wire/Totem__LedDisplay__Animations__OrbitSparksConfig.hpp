#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/OrbitSparks/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::OrbitSparksConfig> {
    using Type = ::Totem::LedDisplay::Animations::OrbitSparksConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::baseHue>{"baseHue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::sparkCount>{"sparkCount"},
        Field<&Type::sparkSize>{"sparkSize"},
        Field<&Type::orbitSpeed>{"orbitSpeed"},
        Field<&Type::radialDrift>{"radialDrift"},
        Field<&Type::highSparkle>{"highSparkle"},
        Field<&Type::peakSensitivity>{"peakSensitivity"},
        Field<&Type::seed>{"seed"},
        Field<&Type::hueModulation>{"hueModulation"}
    );
};

} // namespace Totem::Generated::Wire
