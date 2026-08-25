#pragma once

#include "Generated/Wire/Support.hpp"
#include "AudioFft/Interfaces/Wire.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::AudioFft::BeatEvent> {
    using Type = ::Totem::AudioFft::BeatEvent;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::kind>{"kind"},
        Field<&Type::bpm>{"bpm"},
        Field<&Type::confidence>{"confidence"},
        Field<&Type::energy>{"energy"},
        Field<&Type::sequence>{"sequence"}
    );
};

} // namespace Totem::Generated::Wire
