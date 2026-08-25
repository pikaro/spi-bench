#pragma once

#include "Generated/Wire/Support.hpp"
#include "AudioFft/Interfaces/Wire.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::AudioFft::PeakEvent> {
    using Type = ::Totem::AudioFft::PeakEvent;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::group>{"group"},
        Field<&Type::energy>{"energy"},
        Field<&Type::lowerBand>{"lowerBand"},
        Field<&Type::upperBand>{"upperBand"},
        Field<&Type::frameSequence>{"frameSequence"}
    );
};

} // namespace Totem::Generated::Wire
