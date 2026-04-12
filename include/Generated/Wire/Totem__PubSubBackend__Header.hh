#pragma once

#include "Generated/Wire/Support.hh"
#include "PubSubBackend/Interfaces/Wire.hh"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::PubSubBackend::Header> {
    using Type = ::Totem::PubSubBackend::Header;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::timestampMs>{"timestampMs"},
        Field<&Type::messageId>{"messageId"},
        Field<&Type::topic>{"topic"},
        Field<&Type::source>{"source"},
        Field<&Type::payloadSize>{"payloadSize"}
    );
};

} // namespace Totem::Generated::Wire
