#pragma once

#include "Data/Nodes.hpp"
#include "StaticConfig/PubSub.hpp"
#include <cstdint>
#include <magic_enum/magic_enum.hpp>

namespace Totem::Data::PubSub {

enum class NodeId : uint16_t {
    None = 0,
    Master = 1U << 0,
    Media = 1U << 1,
    InputOutput = 1U << 2,
    GPUNode0 = 1U << 3,
    GPUNode1 = 1U << 4,
    GPUNode2 = 1U << 5,
    GPUNode3 = 1U << 6,
    Power = 1U << 7,
    Host = 1U << 15,
};

// NOLINTNEXTLINE(performance-enum-size)
enum class Topic : uint32_t {
    None = 0,
    Heartbeat = 1U << 0,
    PubSub = 1U << 1,
    Wheel = 1U << 2,
    Beat = 1U << 3,
    FftFrame = 1U << 4,
    Power = 1U << 5,
    Logs = 1U << 6,
    Metrics = 1U << 7,
    Button = 1U << 8,
    AnimationPlay = 1U << 9,
    AnimationUpdate = 1U << 10,
    AnimationStop = 1U << 11,
    AnimationSetHueOffset = 1U << 12,
    AnimationSetRotationOffset = 1U << 13,
    AnimationSetBrightness = 1U << 14,
    AnimationSetLayerActive = 1U << 15,
    AnimationSetLayerOpacity = 1U << 16,
    AnimationFadeLayerSwap = 1U << 17,
    LedPwm = 1U << 18,
    Peak = 1U << 19,
    Dial = 1U << 20,
    Menu = 1U << 21,
    LedOutputReady = 1U << 22,
    LedOutputEnable = 1U << 23,
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
        UDP = 1U << 3,

        // SPI transport IDs name physical buses. HighSpeedSPI currently routes
        // GPU0/GPU1 as peers on one shared bus; future code should keep using
        // bus names rather than inventing one master transport per endpoint.
        MediaSPI = LowSpeedSPI,
        GPU0SPI = HighSpeedSPI,
        GPU1SPI = HighSpeedSPI,
        SPI = LowSpeedSPI,
        WebSocket = UDP,
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

template <> struct NodeTraits<NodeName::Power> {
    static constexpr NodeId nodeId = NodeId::Power;

    enum class Transport : uint8_t {
        None = 0,
        SPI = 1U << 0,
        UDP = 1U << 1,
    };
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

template <>
struct magic_enum::customize::enum_range<Totem::Data::PubSub::NodeId> {
    static constexpr bool is_flags = true;
};

template <>
struct magic_enum::customize::enum_range<Totem::Data::PubSub::Topic> {
    static constexpr bool is_flags = true;
};

static_assert(static_cast<uint16_t>(Totem::Data::PubSub::NodeId::Power) ==
              0x0080U);
static_assert(static_cast<uint16_t>(Totem::Data::PubSub::NodeId::Host) ==
              0x8000U);
static_assert(magic_enum::enum_name(Totem::Data::PubSub::NodeId::Power) ==
              "Power");
static_assert(magic_enum::enum_name(Totem::Data::PubSub::NodeId::Host) ==
              "Host");
