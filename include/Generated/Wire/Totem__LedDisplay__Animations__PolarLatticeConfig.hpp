#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/PolarLattice/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::PolarLatticeConfig> {
    using Type = ::Totem::LedDisplay::Animations::PolarLatticeConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::radialMode>{"radialMode"},
        Field<&Type::angularMode>{"angularMode"},
        Field<&Type::speed>{"speed"},
        Field<&Type::mix>{"mix"},
        Field<&Type::contrast>{"contrast"}
    );
};

} // namespace Totem::Generated::Wire
