#pragma once

#include "Generated/Wire/Support.hpp"
#include "Audio/Interfaces/Wire.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::Audio::FftFrame> {
    using Type = ::Totem::Audio::FftFrame;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::subBass>{"subBass"},
        Field<&Type::bass>{"bass"},
        Field<&Type::lowMid>{"lowMid"},
        Field<&Type::mid>{"mid"},
        Field<&Type::highMid>{"highMid"},
        Field<&Type::presence>{"presence"},
        Field<&Type::brilliance>{"brilliance"},
        Field<&Type::air>{"air"}
    );
};

} // namespace Totem::Generated::Wire
