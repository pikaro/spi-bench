#pragma once

#include "PubSubBackend/detail/Types.hh"
#include "Traits/Bitmask.hh"
#include "Types.hh"
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace Totem::PubSubBackend::detail {

struct Contract {
    using NodeId = typename Spec::NodeId;
    using Topic = typename Spec::Topic;
    using Transport = typename Spec::Transport;
    using Limits = typename Spec::Limits;
    using TopicUnderlying = std::underlying_type_t<Topic>;
    using TransportUnderlying = std::underlying_type_t<Transport>;

    static constexpr size_t kHeaderWireSize = sizeof(uint32_t) +
                                              sizeof(MessageId) +
                                              sizeof(TopicId) +
                                              sizeof(PubSubBackend::NodeId) +
                                              sizeof(uint16_t);
    static constexpr size_t kMinSerializedFrameSize =
        kHeaderWireSize + sizeof(uint32_t);

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

    static_assert(Limits::maxTopics > 0, "Limits::maxTopics must be > 0");
    static_assert(Limits::maxSubscribers > 0,
                  "Limits::maxSubscribers must be > 0");
    static_assert(Limits::maxTransports > 0,
                  "Limits::maxTransports must be > 0");
    static_assert(Limits::maxPayloadSize > 0,
                  "Limits::maxPayloadSize must be > 0");
    static_assert(Limits::maxMessageQueueSize > 0,
                  "Limits::maxMessageQueueSize must be > 0");
    static_assert(Limits::maxInFlightMessages > 0,
                  "Limits::maxInFlightMessages must be > 0");
    static_assert(Limits::ingressBufferSize > 0,
                  "Limits::ingressBufferSize must be > 0");
    static_assert(Limits::defaultEgressBufferSize > 0,
                  "Limits::defaultEgressBufferSize must be > 0");
    static_assert(Limits::maxIngressSpans > 0,
                  "Limits::maxIngressSpans must be > 0");
    static_assert(Limits::maxIngressRecords > 0,
                  "Limits::maxIngressRecords must be > 0");
    static_assert(Limits::maxEgressSpans > 0,
                  "Limits::maxEgressSpans must be > 0");
    static_assert(Limits::maxEgressRecords > 0,
                  "Limits::maxEgressRecords must be > 0");

    static_assert(
        Limits::maxTopics <= std::numeric_limits<TopicUnderlying>::digits,
        "Limits::maxTopics exceeds number of available Topic bitmask bits");
    static_assert(
        Limits::maxTransports <=
            std::numeric_limits<TransportUnderlying>::digits,
        "Limits::maxTransports exceeds number of available Transport "
        "bitmask bits");

    static_assert(Limits::maxPayloadSize <= std::numeric_limits<uint16_t>::max(),
                  "Limits::maxPayloadSize must fit into Header::payloadSize");
    static_assert(Limits::ingressBufferSize >= Limits::maxPayloadSize,
                  "Limits::ingressBufferSize must fit at least one max payload");
    static_assert(Limits::defaultEgressBufferSize >=
                      (kMinSerializedFrameSize + Limits::maxPayloadSize),
                  "Limits::defaultEgressBufferSize must fit at least one "
                  "max-sized serialized frame");
};

} // namespace Totem::PubSubBackend::detail
