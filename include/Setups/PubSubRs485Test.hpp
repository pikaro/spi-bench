#pragma once

#include "Data.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "PubSubBackend/Facade.hpp"
#include "PubSubBackend/Interfaces/Config.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/Transports/BaseTransport.hpp"
#include "PubSubBackend/Transports/Rs485Transport.hpp"
#include "Services/Clock.hpp"
#include "Services/PubSub.hpp"
#include "Setups/PubSubTest.hpp"
#include "Setups/PubSubTestMessage.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "Types/Error.hpp"
#include <cinttypes>
#include <cstdint>
#include <cstring>

template <class Link> struct PubSubRs485TestSetup {
    enum class Role : uint8_t {
        Master,
        Slave,
    };

    using PubSubNode = Totem::PubSubBackend::Node;
    using Transport = NodeData::PubSub::Transport;
    using NodeId = NodeData::PubSub::NodeId;
    using Topic = NodeData::PubSub::Topic;
    using TestPool = Totem::PubSubBackend::Pool<PubSubTest::Message, 8>;
    using Rs485Transport =
        Totem::PubSubBackend::Transports::Rs485Transport<Link>;
    using Rs485Deps =
        Totem::PubSubBackend::Transports::Rs485TransportDependencies<Link>;
    using BaseTransportDeps =
        Totem::PubSubBackend::Transports::BaseTransportDependencies;

    struct Consumer {
        const char *name = nullptr;

        static ReturnCode
        callback(void *ctx, const Totem::PubSubBackend::Envelope &envelope) {
            auto *self = static_cast<Consumer *>(ctx);
            return self->handle(envelope);
        }

        ReturnCode handle(const Totem::PubSubBackend::Envelope &envelope) {
            FAIL_IF_UNEXPECTED_FWD(message,
                                   envelope.getPayloadAs<PubSubTest::Message>(),
                                   "Failed to decode RS485 PubSub message");
            const auto nowUs = ClockService::get().nowUs();
            const auto latencyUs =
                envelope.header.timestampUs == 0
                    ? 0
                    : nowUs - static_cast<int64_t>(envelope.header.timestampUs);
            _log_i("%s received PubSub topic " SV_FMT " message %u in %" PRId64
                   " us: %s",
                   name, MAGIC_SV_ARG(Topic, envelope.header.topic),
                   envelope.header.messageId, latencyUs, message.strVal.data());
            return OK();
        }
    };

    PubSubRs485TestSetup(Totem::TaskController::IRegistry &taskRegistry,
                         Link &link, Role role)
        : pubSubNode(taskRegistry,
                     static_cast<NodeId>(NodeData::PubSub::nodeId)),
          role(role),
          transport(makeRs485Deps(pubSubNode, link, "PubSub-RS485")),
          messagePool(static_cast<void *>(&pubSubNode),
                      PubSubNode::nextMessageId),
          consumer{role == Role::Master ? "RS485-master-consumer"
                                        : "RS485-slave-consumer"} {}

    void setup() {
        ABORT_IF_ERR_BEGIN(pubSubNode.begin(makePubSubConfig("PubSubRs485")));
        ABORT_IF_ERR_BEGIN(transport.begin());
        ABORT_IF_ERR(transport.registerHandler(),
                     "Failed to register PubSub RS485 frame handler");
        ABORT_IF_UNEXPECTED(transportHandle,
                            pubSubNode.registerTransport(transport),
                            "Failed to register PubSub RS485 transport");
        (void)transportHandle;
        PubSubService::set(pubSubNode);

        ABORT_IF_UNEXPECTED(
            subscriptionHandle,
            pubSubNode.subscribe(
                "rs485-sub",
                {.subscriber = &consumer, .callback = Consumer::callback},
                subscribedTopic()),
            "Failed to subscribe PubSub RS485 consumer");
        (void)subscriptionHandle;

        publishStartMs = ::platform::get_time() + warmupMs;
        _log_i("PubSub RS485 setup ready; warming up for %u ms",
               static_cast<unsigned>(warmupMs));
    }

    ReturnCode work(uint32_t nowMs) {
        if (nowMs < publishStartMs || nowMs < nextPublishAtMs) {
            return OK();
        }
        if (!transport.available()) {
            nextPublishAtMs = nowMs + publishIntervalMs;
            return OK();
        }
        if (!ClockService::get().synced()) {
            nextPublishAtMs = nowMs + publishIntervalMs;
            return OK();
        }

        nextPublishAtMs = nowMs + publishIntervalMs;
        auto publishResult = publishTestMessage();
        if (!publishResult.ok()) {
            _log_e("Failed to publish RS485 PubSub message: " ERR_FMT,
                   ERR_ARG(publishResult));
        }
        return OK();
    }

  private:
    [[nodiscard]] Topic publishedTopic() const {
        return role == Role::Master ? Topic::Heartbeat : Topic::Sensor;
    }

    [[nodiscard]] Topic subscribedTopic() const {
        return role == Role::Master ? Topic::Sensor : Topic::Heartbeat;
    }

    ReturnCode publishTestMessage() {
        auto message = PubSubTest::makeTestMessage();
        const auto *label = role == Role::Master ? "RS485 beat from master"
                                                 : "RS485 sensor from slave";
        std::strncpy(message.strVal.data(), label, message.strVal.size() - 1);
        message.strVal[message.strVal.size() - 1] = '\0';

        FAIL_IF_UNEXPECTED_FWD(messageId, messagePool.store(message),
                               "Failed to allocate RS485 PubSub message");
        auto envelopeDef = Totem::PubSubBackend::EnvelopeDef{
            .owner = static_cast<void *>(&messagePool),
            .topic = publishedTopic(),
            .messageId = messageId,
            .source =
                static_cast<Totem::PubSubBackend::NodeId>(pubSubNode.nodeId()),
            .getPayloadPtr = TestPool::getPtr,
            .encodePayload = TestPool::encodePayload,
            .release = TestPool::release,
        };
        auto envelopeResult =
            Totem::PubSubBackend::Envelope::make<PubSubTest::Message>(
                envelopeDef);
        if (!envelopeResult) {
            (void)messagePool.release({.header = {.messageId = messageId}});
            return envelopeResult.error();
        }

        auto publishResult = pubSubNode.publish(*envelopeResult);
        if (!publishResult.ok()) {
            (void)messagePool.release(*envelopeResult);
            return publishResult;
        }
        return OK();
    }

    [[nodiscard]] static Totem::PubSubBackend::Config
    makePubSubConfig(const char *taskName) {
        return Totem::PubSubBackend::Config{
            .task =
                {
                    .name = taskName,
                    .priority = 3,
                    .stackSize = 8192,
                    .intervalMs = 10,
                    .noCatchup = true,
                    .useNotify = true,
                    .notifyTimeoutMs = 10,
                    .autoRestart = false,
                },
        };
    }

    [[nodiscard]] static BaseTransportDeps makeBaseDeps(PubSubNode &node,
                                                        const char *name) {
        return BaseTransportDeps{
            .pubSubNode = static_cast<void *>(&node),
            .transportId = static_cast<uint8_t>(Transport::RS485),
            .name = name,
            .sendAckCallback = PubSubNode::ack,
            .availabilityObserver = &node,
            .wakeCallback = PubSubNode::wake,
            .ingressDispatchCallback = PubSubNode::dispatchIngressFrame,
            .ingress = node.ingress(),
        };
    }

    [[nodiscard]] static Rs485Deps makeRs485Deps(PubSubNode &node, Link &link,
                                                 const char *name) {
        return Rs485Deps{
            .base = makeBaseDeps(node, name),
            .link = link,
        };
    }

    PubSubNode pubSubNode;
    Role role;
    Rs485Transport transport;
    TestPool messagePool;
    Consumer consumer;

    uint32_t publishStartMs = 0;
    uint32_t nextPublishAtMs = 0;

    static constexpr uint32_t warmupMs = 2000;
    static constexpr uint32_t publishIntervalMs = 10;
};
