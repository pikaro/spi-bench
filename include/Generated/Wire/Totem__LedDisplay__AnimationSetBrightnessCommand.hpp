#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::AnimationSetBrightnessCommand> {
    using Type = ::Totem::LedDisplay::AnimationSetBrightnessCommand;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::value>{"value"}
    );
};

} // namespace Totem::Generated::Wire
