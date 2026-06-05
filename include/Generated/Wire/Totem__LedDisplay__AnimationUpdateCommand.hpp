#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::AnimationUpdateCommand> {
    using Type = ::Totem::LedDisplay::AnimationUpdateCommand;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::kind>{"kind"},
        Field<&Type::requestId>{"requestId"},
        Field<&Type::payloadSize>{"payloadSize"},
        Field<&Type::payload>{"payload"}
    );
};

} // namespace Totem::Generated::Wire
