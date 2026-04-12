#pragma once

#include "PubSubBackend/detail/Types.hh"
#include "Traits/Bitmask.hh"
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace Totem::PubSubBackend::detail {

struct Contract {
    using NodeId = typename Spec::NodeId;
    using Topic = typename Spec::Topic;
    using Transport = typename Spec::Transport;
    using Limits = typename Spec::Limits;

    static_assert(std::is_enum_v<NodeId>, "Spec::NodeId must be an enum");
    static_assert(std::same_as<std::underlying_type_t<NodeId>, uint8_t>,
                  "Spec::NodeId must have uint8_t as underlying type");
    static_assert(IsBitmaskEnum<NodeId>, "Spec::NodeId must be a bitmask enum");

    static_assert(std::is_enum_v<Topic>, "Spec::Topic must be an enum");
    static_assert(std::same_as<std::underlying_type_t<Topic>, uint32_t>,
                  "Spec::Topic must have uint32_t as underlying type");
    static_assert(IsBitmaskEnum<Topic>, "Spec::Topic must be a bitmask enum");

    static_assert(std::is_enum_v<Transport>, "Spec::Transport must be an enum");
    static_assert(std::same_as<std::underlying_type_t<Transport>, uint8_t>,
                  "Spec::Transport must have uint8_t as underlying type");
    static_assert(IsBitmaskEnum<Transport>,
                  "Spec::Transport must be a bitmask enum");
};

} // namespace Totem::PubSubBackend::detail
