#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::AnimationPlayCommand> {
    using Type = ::Totem::LedDisplay::AnimationPlayCommand;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::kind>{"kind"},
        Field<&Type::requestId>{"requestId"},
        Field<&Type::layer>{"layer"},
        Field<&Type::lifetimeMs>{"lifetimeMs"},
        Field<&Type::payloadSize>{"payloadSize"},
        Field<&Type::payload>{"payload"}
    );
};

} // namespace Totem::Generated::Wire
