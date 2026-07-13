#pragma once

#include "Generated/Wire/Support.hpp"
#include "AudioFft/Interfaces/Wire.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::AudioFft::FftFrame> {
    using Type = ::Totem::AudioFft::FftFrame;
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
