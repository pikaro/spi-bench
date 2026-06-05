#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/StainedCells/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::StainedCellsConfig> {
    using Type = ::Totem::LedDisplay::Animations::StainedCellsConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::baseHue>{"baseHue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::baseValue>{"baseValue"},
        Field<&Type::seedCount>{"seedCount"},
        Field<&Type::borderWidth>{"borderWidth"},
        Field<&Type::interiorValue>{"interiorValue"},
        Field<&Type::driftSpeed>{"driftSpeed"},
        Field<&Type::contrast>{"contrast"},
        Field<&Type::peakSensitivity>{"peakSensitivity"},
        Field<&Type::seed>{"seed"},
        Field<&Type::hueModulation>{"hueModulation"}
    );
};

} // namespace Totem::Generated::Wire
