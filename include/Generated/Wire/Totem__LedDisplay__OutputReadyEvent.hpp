#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Interfaces/OutputStartup.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::OutputReadyEvent> {
    using Type = ::Totem::LedDisplay::OutputReadyEvent;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::ready>{"ready"}
    );
};

} // namespace Totem::Generated::Wire
