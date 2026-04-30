#pragma once

#include "Data.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "PubSubBackend/Facade.hpp"
#include "PubSubBackend/Interfaces/Config.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/Transports/BaseTransport.hpp"
#include "PubSubBackend/Transports/SpiTransport.hpp"
#include "Services/Clock.hpp"
#include "Services/PubSub.hpp"
#include "Setups/PubSubTest.hpp"
#include "Setups/PubSubTestMessage.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "Types/Error.hpp"
#include <cinttypes>
#include <cstdint>
#include <cstring>

template <class Link> struct PubSubSpiTestSetup {
    enum class Role : uint8_t {
        Master,
        Slave,
    };

    using PubSubNode = Totem::PubSubBackend::Node;
    using Transport = NodeData::PubSub::Transport;
    using NodeId = NodeData::PubSub::NodeId;
    using Topic = NodeData::PubSub::Topic;
    using TestPool = Totem::PubSubBackend::Pool<PubSubTest::Message, 8>;
    using SpiTransport = Totem::PubSubBackend::Transports::SpiTransport<Link>;
    using SpiDeps =
        Totem::PubSubBackend::Transports::SpiTransportDependencies<Link>;
    using BaseTransportDeps =
        Totem::PubSubBackend::Transports::BaseTransportDependencies;

    struct Config {
        uint32_t masterPublishIntervalMs = 10;
        uint32_t slavePublishIntervalMs = 10;
        uint32_t reportIntervalMs = 1000;
        bool masterPublishes = false;
        bool slavePublishes = true;
    };

    struct Stats {
        uint32_t publishAttempts = 0;
        uint32_t publishFailures = 0;
        uint32_t publishOverflow = 0;
        uint32_t received = 0;
        uint32_t receivedBytes = 0;
        int64_t minLatencyUs = INT64_MAX;
        int64_t maxLatencyUs = INT64_MIN;
        int64_t totalLatencyUs = 0;

        void recordLatency(int64_t latencyUs, uint16_t payloadSize) {
            ++received;
            receivedBytes += payloadSize;
            if (latencyUs < minLatencyUs) {
                minLatencyUs = latencyUs;
            }
            if (latencyUs > maxLatencyUs) {
                maxLatencyUs = latencyUs;
            }
            totalLatencyUs += latencyUs;
        }
    };

    struct Consumer {
        const char *name = nullptr;
        Stats *stats = nullptr;

        static ReturnCode
        callback(void *ctx, const Totem::PubSubBackend::Envelope &envelope) {
            auto *self = static_cast<Consumer *>(ctx);
            return self->handle(envelope);
        }

        ReturnCode handle(const Totem::PubSubBackend::Envelope &envelope) {
            FAIL_IF_UNEXPECTED_FWD(message,
                                   envelope.getPayloadAs<PubSubTest::Message>(),
                                   "Failed to decode SPI PubSub message");
            const auto nowUs = ClockService::get().nowUs();
            const auto latencyUs =
                envelope.header.timestampUs == 0
                    ? 0
                    : nowUs - static_cast<int64_t>(envelope.header.timestampUs);
            if (stats != nullptr) {
                stats->recordLatency(latencyUs, envelope.header.payloadSize);
            }
            return OK();
        }
    };

    PubSubSpiTestSetup(Totem::TaskController::IRegistry &taskRegistry,
                       Link &link, Role role, Config config = {})
        : pubSubNode(taskRegistry,
                     static_cast<NodeId>(NodeData::PubSub::nodeId)),
          role(role), config(config),
          transport(makeSpiDeps(pubSubNode, link, "PubSub-SPI")),
          messagePool(static_cast<void *>(&pubSubNode),
                      PubSubNode::nextMessageId),
          consumer{role == Role::Master ? "SPI-master-consumer"
                                        : "SPI-slave-consumer",
                   &stats} {}

    void setup() {
        ABORT_IF_ERR_BEGIN(pubSubNode.begin(makePubSubConfig("PubSubSpi")));
        ABORT_IF_ERR_BEGIN(transport.begin());
        ABORT_IF_ERR(transport.registerHandler(),
                     "Failed to register PubSub SPI frame handler");
        ABORT_IF_UNEXPECTED(transportHandle,
                            pubSubNode.registerTransport(transport),
                            "Failed to register PubSub SPI transport");
        (void)transportHandle;
        PubSubService::set(pubSubNode);

        ABORT_IF_UNEXPECTED(
            subscriptionHandle,
            pubSubNode.subscribe(
                "spi-sub",
                {.subscriber = &consumer, .callback = Consumer::callback},
                subscribedTopic()),
            "Failed to subscribe PubSub SPI consumer");
        (void)subscriptionHandle;

        publishStartMs = ::platform::get_time() + warmupMs;
        _log_i("PubSub SPI setup ready; warming up for %u ms",
               static_cast<unsigned>(warmupMs));
    }

    ReturnCode work(uint32_t nowMs) {
        reportIfDue(nowMs);
        if (nowMs < publishStartMs || nowMs < nextPublishAtMs) {
            return OK();
        }
        nextPublishAtMs = nowMs + publishIntervalMs();
        if (!publishingEnabled()) {
            return OK();
        }
        if (!transport.available()) {
            return OK();
        }
        if (!ClockService::get().synced()) {
            return OK();
        }

        ++stats.publishAttempts;
        auto publishResult = publishTestMessage();
        if (!publishResult.ok()) {
            ++stats.publishFailures;
            if (publishResult == ERR(CoreError, Overflow)) {
                ++stats.publishOverflow;
            }
        }
        return OK();
    }

  private:
    [[nodiscard]] bool publishingEnabled() const {
        return role == Role::Master ? config.masterPublishes
                                    : config.slavePublishes;
    }

    [[nodiscard]] uint32_t publishIntervalMs() const {
        return role == Role::Master ? config.masterPublishIntervalMs
                                    : config.slavePublishIntervalMs;
    }

    [[nodiscard]] Topic publishedTopic() const {
        return role == Role::Master ? Topic::Heartbeat : Topic::Sensor;
    }

    [[nodiscard]] Topic subscribedTopic() const {
        return role == Role::Master ? Topic::Sensor : Topic::Heartbeat;
    }

    ReturnCode publishTestMessage() {
        auto message = PubSubTest::makeTestMessage();
        const auto *label = role == Role::Master ? "SPI beat from master"
                                                 : "SPI sensor from slave";
        std::strncpy(message.strVal.data(), label, message.strVal.size() - 1);
        message.strVal[message.strVal.size() - 1] = '\0';

        FAIL_IF_UNEXPECTED_FWD(messageId, messagePool.store(message),
                               "Failed to allocate SPI PubSub message");
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

    void reportIfDue(uint32_t nowMs) {
        if (config.reportIntervalMs == 0 ||
            nowMs - lastReportAtMs < config.reportIntervalMs) {
            return;
        }

        const auto elapsedMs = lastReportAtMs == 0 ? config.reportIntervalMs
                                                   : nowMs - lastReportAtMs;
        lastReportAtMs = nowMs;

        const auto period = stats;
        stats = {};

        const auto avgLatencyUs =
            period.received == 0
                ? 0
                : period.totalLatencyUs / static_cast<int64_t>(period.received);
        const auto minLatencyUs =
            period.received == 0 ? 0 : period.minLatencyUs;
        const auto maxLatencyUs =
            period.received == 0 ? 0 : period.maxLatencyUs;
        const auto rxPerSecond =
            elapsedMs == 0 ? 0 : (period.received * 1000U) / elapsedMs;
        const auto rxBytesPerSecond =
            elapsedMs == 0 ? 0 : (period.receivedBytes * 1000U) / elapsedMs;
        const auto txPerSecond =
            elapsedMs == 0 ? 0 : (period.publishAttempts * 1000U) / elapsedMs;

        _log_i("%s stats: tx=%" PRIu32 "/s txFail=%" PRIu32
               " txOverflow=%" PRIu32 " rx=%" PRIu32 "/s rxBytes=%" PRIu32
               "/s latencyUs[min/avg/max]=%" PRId64 "/%" PRId64 "/%" PRId64,
               consumer.name, txPerSecond, period.publishFailures,
               period.publishOverflow, rxPerSecond, rxBytesPerSecond,
               minLatencyUs, avgLatencyUs, maxLatencyUs);
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
            .transportId = static_cast<uint8_t>(Transport::SPI),
            .name = name,
            .sendAckCallback = PubSubNode::ack,
            .availabilityObserver = &node,
            .wakeCallback = PubSubNode::wake,
            .ingressDispatchCallback = PubSubNode::dispatchIngressFrame,
            .ingress = node.ingress(),
        };
    }

    [[nodiscard]] static SpiDeps makeSpiDeps(PubSubNode &node, Link &link,
                                             const char *name) {
        return SpiDeps{
            .base = makeBaseDeps(node, name),
            .link = link,
        };
    }

    PubSubNode pubSubNode;
    Role role;
    Config config;
    SpiTransport transport;
    TestPool messagePool;
    Consumer consumer;
    Stats stats{};

    uint32_t publishStartMs = 0;
    uint32_t nextPublishAtMs = 0;
    uint32_t lastReportAtMs = 0;

    static constexpr uint32_t warmupMs = 2000;
};
