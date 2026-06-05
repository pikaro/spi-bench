#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Interfaces/LayerControl.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::LayerActive> {
    using Type = ::Totem::LedDisplay::LayerActive;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::layer>{"layer"},
        Field<&Type::active>{"active"}
    );
};

} // namespace Totem::Generated::Wire
