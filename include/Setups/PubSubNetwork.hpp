#pragma once

#include "Data/PubSub.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Facade.hpp"
#include "PubSubBackend/Interfaces/Config.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/Transports/BaseTransport.hpp"
#include "PubSubBackend/Transports/Rs485Transport.hpp"
#include "PubSubBackend/Transports/SpiRouterTransport.hpp"
#include "PubSubBackend/Transports/SpiTransport.hpp"
#include "Services/PubSub.hpp"
#include "StaticConfig/Stacks.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include <cstdint>

namespace PubSubNetwork {

using PubSubNode = Totem::PubSubBackend::Node;
using BaseTransportDeps =
    Totem::PubSubBackend::Transports::BaseTransportDependencies;

struct TaskConfig {
    const char *name = "PubSub";
    uint8_t priority = 3;
    Totem::TaskController::Config::CorePreference core =
        Totem::TaskController::Config::CorePreference::any();
    uint32_t stackSize = Totem::StaticConfig::TaskStacks::pubSubNode;
    uint32_t intervalMs = 5;
    uint32_t subscriptionReplayIntervalMs = 1000;
};

[[nodiscard]] inline Totem::PubSubBackend::Config
makePubSubConfig(const TaskConfig &config) {
    return Totem::PubSubBackend::Config{
        .task =
            {
                .name = config.name,
                .priority = config.priority,
                .core = config.core,
                .stackSize = config.stackSize,
                .intervalMs = config.intervalMs,
                .noCatchup = true,
                .useNotify = true,
                .notifyTimeoutMs = config.intervalMs,
                .autoRestart = false,
            },
        .subscriptionReplayIntervalMs = config.subscriptionReplayIntervalMs,
    };
}

[[nodiscard]] inline BaseTransportDeps
makeBaseDeps(PubSubNode &node, uint8_t transportId, const char *name) {
    return BaseTransportDeps{
        .pubSubNode = static_cast<void *>(&node),
        .transportId = transportId,
        .name = name,
        .sendAckCallback = PubSubNode::ack,
        .availabilityObserver = &node,
        .wakeCallback = PubSubNode::wake,
        .ingressDispatchCallback = PubSubNode::dispatchIngressFrame,
        .ingress = node.ingress(),
    };
}

} // namespace PubSubNetwork

template <class LowSpeedSpiLink, class HighSpeedSpiLink, class Rs485Link>
struct PubSubNetworkMasterSetup {
    using MasterPubSub =
        Totem::Data::PubSub::PubSubData<Totem::Data::NodeName::Master>;
    using Transport = MasterPubSub::Transport;
    using LowSpeedSpiTransport =
        Totem::PubSubBackend::Transports::SpiTransport<LowSpeedSpiLink>;
    using HighSpeedSpiTransport =
        Totem::PubSubBackend::Transports::SpiRouterTransport<HighSpeedSpiLink,
                                                             2>;
    using Rs485Transport =
        Totem::PubSubBackend::Transports::Rs485Transport<Rs485Link>;
    using LowSpeedSpiDeps =
        Totem::PubSubBackend::Transports::SpiTransportDependencies<
            LowSpeedSpiLink>;
    using HighSpeedSpiDeps =
        Totem::PubSubBackend::Transports::SpiRouterTransportDependencies<
            HighSpeedSpiLink, 2>;
    using Rs485Deps =
        Totem::PubSubBackend::Transports::Rs485TransportDependencies<Rs485Link>;

    struct Config {
        PubSubNetwork::TaskConfig task{
            .name = "PubSub",
            .core = Totem::TaskController::Config::CorePreference::specific(1),
        };
    };

    PubSubNetworkMasterSetup(Totem::TaskController::IRegistry &taskRegistry,
                             LowSpeedSpiLink &lowSpeedSpiLink,
                             HighSpeedSpiLink &gpu0SpiLink,
                             HighSpeedSpiLink &gpu1SpiLink,
                             Rs485Link &rs485Link, Config config = {})
        : pubSubNode(
              taskRegistry,
              static_cast<Totem::Data::PubSub::NodeId>(MasterPubSub::nodeId)),
          config(config), lowSpeedSpiTransport(
                              makeLowSpeedSpiDeps(pubSubNode, lowSpeedSpiLink)),
          highSpeedSpiTransport(
              makeHighSpeedSpiDeps(pubSubNode, gpu0SpiLink, gpu1SpiLink)),
          rs485Transport(makeRs485Deps(pubSubNode, rs485Link)) {}

    void setup() {
        ABORT_IF_ERR_BEGIN(
            pubSubNode.begin(PubSubNetwork::makePubSubConfig(config.task)));

        ABORT_IF_ERR_BEGIN(lowSpeedSpiTransport.begin());
        ABORT_IF_ERR(lowSpeedSpiTransport.registerHandler(),
                     "Failed to register low-speed SPI PubSub frame handler");
        ABORT_IF_UNEXPECTED(
            lowSpeedHandle, pubSubNode.registerTransport(lowSpeedSpiTransport),
            "Failed to register low-speed SPI PubSub transport");
        (void)lowSpeedHandle;

        ABORT_IF_ERR_BEGIN(highSpeedSpiTransport.begin());
        ABORT_IF_ERR(highSpeedSpiTransport.registerHandler(),
                     "Failed to register high-speed SPI PubSub router "
                     "frame handlers");
        ABORT_IF_UNEXPECTED(
            highSpeedHandle,
            pubSubNode.registerTransport(highSpeedSpiTransport),
            "Failed to register high-speed SPI PubSub router transport");
        (void)highSpeedHandle;

        ABORT_IF_ERR_BEGIN(rs485Transport.begin());
        ABORT_IF_ERR(rs485Transport.registerHandler(),
                     "Failed to register IO RS485 PubSub frame handler");
        ABORT_IF_UNEXPECTED(rs485Handle,
                            pubSubNode.registerTransport(rs485Transport),
                            "Failed to register IO RS485 PubSub transport");
        (void)rs485Handle;

        PubSubService::set(pubSubNode);
        _log_i("PubSub network master setup ready");
    }

    [[nodiscard]] PubSubNetwork::PubSubNode &node() { return pubSubNode; }

  private:
    [[nodiscard]] static LowSpeedSpiDeps
    makeLowSpeedSpiDeps(PubSubNetwork::PubSubNode &node,
                        LowSpeedSpiLink &link) {
        return LowSpeedSpiDeps{
            .base = PubSubNetwork::makeBaseDeps(
                node, static_cast<uint8_t>(Transport::LowSpeedSPI),
                "PubSub-LowSpeedSPI"),
            .link = link,
        };
    }

    [[nodiscard]] static HighSpeedSpiDeps
    makeHighSpeedSpiDeps(PubSubNetwork::PubSubNode &node,
                         HighSpeedSpiLink &gpu0Link,
                         HighSpeedSpiLink &gpu1Link) {
        using PeerDeps =
            Totem::PubSubBackend::Transports::SpiRouterPeerDependencies<
                HighSpeedSpiLink>;
        using NodeId = Totem::Data::PubSub::NodeId;
        return HighSpeedSpiDeps{
            .pubSubNode = static_cast<void *>(&node),
            .transportId = static_cast<uint8_t>(Transport::HighSpeedSPI),
            .name = "PubSub-HighSpeedSPI",
            .sendAckCallback = PubSubNetwork::PubSubNode::ack,
            .availabilityObserver = &node,
            .wakeCallback = PubSubNetwork::PubSubNode::wake,
            .ingressDispatchCallback =
                PubSubNetwork::PubSubNode::dispatchIngressFrame,
            .ingress = &node.ingress(),
            .peers =
                {
                    PeerDeps{
                        .peerId = static_cast<Totem::PubSubBackend::PeerId>(
                            NodeId::GPUNode0),
                        .link = &gpu0Link,
                        .name = "gpu0",
                    },
                    PeerDeps{
                        .peerId = static_cast<Totem::PubSubBackend::PeerId>(
                            NodeId::GPUNode1),
                        .link = &gpu1Link,
                        .name = "gpu1",
                    },
                },
        };
    }

    [[nodiscard]] static Rs485Deps
    makeRs485Deps(PubSubNetwork::PubSubNode &node, Rs485Link &link) {
        return Rs485Deps{
            .base = PubSubNetwork::makeBaseDeps(
                node, static_cast<uint8_t>(Transport::RS485), "PubSub-RS485"),
            .link = link,
        };
    }

    PubSubNetwork::PubSubNode pubSubNode;
    Config config;
    LowSpeedSpiTransport lowSpeedSpiTransport;
    HighSpeedSpiTransport highSpeedSpiTransport;
    Rs485Transport rs485Transport;
};

template <class Link, Totem::Data::NodeName Node>
struct PubSubNetworkSpiEdgeSetup {
    using NodePubSub = Totem::Data::PubSub::PubSubData<Node>;
    using Transport = typename NodePubSub::Transport;
    using SpiTransport = Totem::PubSubBackend::Transports::SpiTransport<Link>;
    using SpiDeps =
        Totem::PubSubBackend::Transports::SpiTransportDependencies<Link>;

    struct Config {
        PubSubNetwork::TaskConfig task{};
    };

    PubSubNetworkSpiEdgeSetup(Totem::TaskController::IRegistry &taskRegistry,
                              Link &link)
        : PubSubNetworkSpiEdgeSetup(taskRegistry, link, defaultConfig()) {}

    PubSubNetworkSpiEdgeSetup(Totem::TaskController::IRegistry &taskRegistry,
                              Link &link, Config config)
        : pubSubNode(
              taskRegistry,
              static_cast<Totem::Data::PubSub::NodeId>(NodePubSub::nodeId)),
          config(config),
          transport(makeSpiDeps(pubSubNode, link, "PubSub-SPI")) {}

    void setup() {
        ABORT_IF_ERR_BEGIN(
            pubSubNode.begin(PubSubNetwork::makePubSubConfig(config.task)));
        ABORT_IF_ERR_BEGIN(transport.begin());
        ABORT_IF_ERR(transport.registerHandler(),
                     "Failed to register SPI PubSub frame handler");
        ABORT_IF_UNEXPECTED(transportHandle,
                            pubSubNode.registerTransport(transport),
                            "Failed to register SPI PubSub transport");
        (void)transportHandle;
        PubSubService::set(pubSubNode);

        _log_i("PubSub SPI edge setup ready");
    }

  private:
    [[nodiscard]] static constexpr Config defaultConfig() {
        auto config = PubSubNetwork::TaskConfig{};
        if constexpr (Node == Totem::Data::NodeName::GPUNode0 ||
                      Node == Totem::Data::NodeName::GPUNode1 ||
                      Node == Totem::Data::NodeName::GPUNode2 ||
                      Node == Totem::Data::NodeName::GPUNode3) {
            config.core =
                Totem::TaskController::Config::CorePreference::specific(0);
        }
        return Config{.task = config};
    }

    [[nodiscard]] static SpiDeps makeSpiDeps(PubSubNetwork::PubSubNode &node,
                                             Link &link, const char *name) {
        return SpiDeps{
            .base = PubSubNetwork::makeBaseDeps(
                node, static_cast<uint8_t>(Transport::SPI), name),
            .link = link,
        };
    }

    PubSubNetwork::PubSubNode pubSubNode;
    Config config;
    SpiTransport transport;
};

template <class Link> struct PubSubNetworkRs485EdgeSetup {
    using NodePubSub =
        Totem::Data::PubSub::PubSubData<Totem::Data::NodeName::InputOutput>;
    using Transport = typename NodePubSub::Transport;
    using Rs485Transport =
        Totem::PubSubBackend::Transports::Rs485Transport<Link>;
    using Rs485Deps =
        Totem::PubSubBackend::Transports::Rs485TransportDependencies<Link>;

    struct Config {
        PubSubNetwork::TaskConfig task{};
    };

    PubSubNetworkRs485EdgeSetup(Totem::TaskController::IRegistry &taskRegistry,
                                Link &link, Config config = {})
        : pubSubNode(
              taskRegistry,
              static_cast<Totem::Data::PubSub::NodeId>(NodePubSub::nodeId)),
          config(config),
          transport(makeRs485Deps(pubSubNode, link, "PubSub-RS485")) {}

    void setup() {
        ABORT_IF_ERR_BEGIN(
            pubSubNode.begin(PubSubNetwork::makePubSubConfig(config.task)));
        ABORT_IF_ERR_BEGIN(transport.begin());
        ABORT_IF_ERR(transport.registerHandler(),
                     "Failed to register RS485 PubSub frame handler");
        ABORT_IF_UNEXPECTED(transportHandle,
                            pubSubNode.registerTransport(transport),
                            "Failed to register RS485 PubSub transport");
        (void)transportHandle;
        PubSubService::set(pubSubNode);

        _log_i("PubSub RS485 edge setup ready");
    }

  private:
    [[nodiscard]] static Rs485Deps
    makeRs485Deps(PubSubNetwork::PubSubNode &node, Link &link,
                  const char *name) {
        return Rs485Deps{
            .base = PubSubNetwork::makeBaseDeps(
                node, static_cast<uint8_t>(Transport::RS485), name),
            .link = link,
        };
    }

    PubSubNetwork::PubSubNode pubSubNode;
    Config config;
    Rs485Transport transport;
};
