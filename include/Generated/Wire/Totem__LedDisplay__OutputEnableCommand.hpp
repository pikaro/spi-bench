#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Interfaces/OutputStartup.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::OutputEnableCommand> {
    using Type = ::Totem::LedDisplay::OutputEnableCommand;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::enabled>{"enabled"}
    );
};

} // namespace Totem::Generated::Wire
