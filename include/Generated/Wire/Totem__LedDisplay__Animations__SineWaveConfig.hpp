#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/SineWave/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::SineWaveConfig> {
    using Type = ::Totem::LedDisplay::Animations::SineWaveConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::baseValue>{"baseValue"},
        Field<&Type::width>{"width"},
        Field<&Type::durationMs>{"durationMs"},
        Field<&Type::wavelength>{"wavelength"},
        Field<&Type::outerOrigin>{"outerOrigin"},
        Field<&Type::travelRings>{"travelRings"},
        Field<&Type::spokeGainPct>{"spokeGainPct"},
        Field<&Type::tailDecay>{"tailDecay"},
        Field<&Type::peakHold>{"peakHold"}
    );
};

} // namespace Totem::Generated::Wire
