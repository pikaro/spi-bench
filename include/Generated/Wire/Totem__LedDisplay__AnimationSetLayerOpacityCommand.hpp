#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::AnimationSetLayerOpacityCommand> {
    using Type = ::Totem::LedDisplay::AnimationSetLayerOpacityCommand;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::layer>{"layer"},
        Field<&Type::opacity>{"opacity"}
    );
};

} // namespace Totem::Generated::Wire
