#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/Shutter/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::ShutterConfig> {
    using Type = ::Totem::LedDisplay::Animations::ShutterConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::hue>{"hue"},
        Field<&Type::saturation>{"saturation"},
        Field<&Type::value>{"value"},
        Field<&Type::segments>{"segments"},
        Field<&Type::openPct>{"openPct"},
        Field<&Type::edgeWidth>{"edgeWidth"},
        Field<&Type::rotationCycles>{"rotationCycles"},
        Field<&Type::mode>{"mode"}
    );
};

} // namespace Totem::Generated::Wire
