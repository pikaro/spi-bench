#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::AnimationSetHueOffsetCommand> {
    using Type = ::Totem::LedDisplay::AnimationSetHueOffsetCommand;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::offset>{"offset"}
    );
};

} // namespace Totem::Generated::Wire
