#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Interfaces/LayerControl.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::LayerOpacity> {
    using Type = ::Totem::LedDisplay::LayerOpacity;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::layer>{"layer"},
        Field<&Type::opacity>{"opacity"}
    );
};

} // namespace Totem::Generated::Wire
