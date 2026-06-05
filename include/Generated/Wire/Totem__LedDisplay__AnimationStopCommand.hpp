#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::AnimationStopCommand> {
    using Type = ::Totem::LedDisplay::AnimationStopCommand;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::requestId>{"requestId"}
    );
};

} // namespace Totem::Generated::Wire
