#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::AnimationSetLayerActiveCommand> {
    using Type = ::Totem::LedDisplay::AnimationSetLayerActiveCommand;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::layer>{"layer"},
        Field<&Type::active>{"active"}
    );
};

} // namespace Totem::Generated::Wire
