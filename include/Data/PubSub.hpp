#pragma once

#include "Data/Nodes.hpp"
#include "StaticConfig/PubSub.hpp"
#include <cstdint>

namespace Totem::Data::PubSub {

enum class NodeId : uint8_t {
    None = 0,
    Master = 1U << 0,
    Media = 1U << 1,
    InputOutput = 1U << 2,
    GPUNode0 = 1U << 3,
    GPUNode1 = 1U << 4,
    GPUNode2 = 1U << 5,
    GPUNode3 = 1U << 6,
};

// NOLINTNEXTLINE(performance-enum-size)
enum class Topic : uint32_t {
    None = 0,
    Heartbeat = 1U << 0,
    PubSub = 1U << 1,
    Sensor = 1U << 2,
    Beat = 1U << 3,
    FftFrame = 1U << 4,
    Power = 1U << 5,
    Logs = 1U << 6,
    Metrics = 1U << 7,
    Button = 1U << 8,
};

enum class SPIOnlyTransport : uint8_t {
    None = 0,
    SPI = 1U << 0,
};

template <NodeName> struct NodeTraits;

template <> struct NodeTraits<NodeName::Master> {
    static constexpr NodeId nodeId = NodeId::Master;

    enum class Transport : uint8_t {
        None = 0,
        LowSpeedSPI = 1U << 0,
        HighSpeedSPI = 1U << 1,
        RS485 = 1U << 2,
        WebSocket = 1U << 3,

        // Current hardware has one attached peer on each SPI bus, but the
        // target topology is multi-peer per bus. Prefer bus names for new code.
        MediaSPI = LowSpeedSPI,
        GPU0SPI = HighSpeedSPI,
        SPI = LowSpeedSPI,
    };
    using Limits = PubSubConfig;
};

template <> struct NodeTraits<NodeName::Media> {
    static constexpr NodeId nodeId = NodeId::Media;

    using Transport = SPIOnlyTransport;
    using Limits = PubSubConfig;
};

template <> struct NodeTraits<NodeName::InputOutput> {
    static constexpr NodeId nodeId = NodeId::InputOutput;

    enum class Transport : uint8_t {
        None = 0,
        RS485 = 1U << 0,
    };
    using Limits = PubSubConfig;
};

template <> struct NodeTraits<NodeName::GPUNode0> {
    static constexpr NodeId nodeId = NodeId::GPUNode0;

    using Transport = SPIOnlyTransport;
    using Limits = PubSubConfig;
};

template <> struct NodeTraits<NodeName::GPUNode1> {
    static constexpr NodeId nodeId = NodeId::GPUNode1;

    using Transport = SPIOnlyTransport;
    using Limits = PubSubConfig;
};

template <> struct NodeTraits<NodeName::GPUNode2> {
    static constexpr NodeId nodeId = NodeId::GPUNode2;

    using Transport = SPIOnlyTransport;
    using Limits = PubSubConfig;
};

template <> struct NodeTraits<NodeName::GPUNode3> {
    static constexpr NodeId nodeId = NodeId::GPUNode3;

    using Transport = SPIOnlyTransport;
    using Limits = PubSubConfig;
};

template <NodeName N> struct PubSubData {
    using NodeId = Totem::Data::PubSub::NodeId;
    using Topic = Totem::Data::PubSub::Topic;
    using Transport = typename NodeTraits<N>::Transport;
    using Limits = typename NodeTraits<N>::Limits;

    static constexpr auto nodeName = N;
    static constexpr NodeId nodeId = NodeTraits<N>::nodeId;
};

} // namespace Totem::Data::PubSub
