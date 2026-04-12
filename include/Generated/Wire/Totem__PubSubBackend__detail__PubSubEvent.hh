#pragma once

#include "Generated/Wire/Support.hh"
#include "PubSubBackend/detail/Wire.hh"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::PubSubBackend::detail::PubSubEvent> {
    using Type = ::Totem::PubSubBackend::detail::PubSubEvent;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::topic>{"topic"},
        Field<&Type::type>{"type"}
    );
};

} // namespace Totem::Generated::Wire
