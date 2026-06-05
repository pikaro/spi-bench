#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/SpectralIris/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::SpectralIrisConfig> {
    using Type = ::Totem::LedDisplay::Animations::SpectralIrisConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::baseHue>{"baseHue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::baseValue>{"baseValue"},
        Field<&Type::petals>{"petals"},
        Field<&Type::aperture>{"aperture"},
        Field<&Type::rimWidth>{"rimWidth"},
        Field<&Type::contrast>{"contrast"},
        Field<&Type::peakSensitivity>{"peakSensitivity"},
        Field<&Type::flowSpeed>{"flowSpeed"},
        Field<&Type::hueModulation>{"hueModulation"}
    );
};

} // namespace Totem::Generated::Wire
