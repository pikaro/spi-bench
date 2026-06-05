#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::AnimationFadeLayerSwapCommand> {
    using Type = ::Totem::LedDisplay::AnimationFadeLayerSwapCommand;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::first>{"first"},
        Field<&Type::second>{"second"},
        Field<&Type::durationMs>{"durationMs"}
    );
};

} // namespace Totem::Generated::Wire
