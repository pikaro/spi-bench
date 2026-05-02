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
#include "PubSubBackend/Transports/SpiTransport.hpp"
#include "Services/Clock.hpp"
#include "Services/PubSub.hpp"
#include "Setups/PubSubTestMessage.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace PubSubStarTest {

using NodeId = NodeData::PubSub::NodeId;
using Topic = NodeData::PubSub::Topic;
using PubSubNode = Totem::PubSubBackend::Node;
using BaseTransportDeps =
    Totem::PubSubBackend::Transports::BaseTransportDependencies;

inline constexpr int64_t targetLatencyUs = 10000;
inline constexpr std::array<int64_t, 17> latencyBucketUpperUs{
    500,  1000, 2000, 3000, 4000,  5000,  6000,  7000, 8000,
    9000, 10000, 12000, 15000, 20000, 30000, 50000, 100000};

struct LatencyStats {
    uint32_t received = 0;
    uint32_t receivedBytes = 0;
    uint32_t targetMisses = 0;
    int64_t minLatencyUs = std::numeric_limits<int64_t>::max();
    int64_t maxLatencyUs = std::numeric_limits<int64_t>::min();
    int64_t totalLatencyUs = 0;
    std::array<uint32_t, latencyBucketUpperUs.size() + 1> latencyBuckets{};

    void recordLatency(int64_t latencyUs, uint16_t payloadSize) {
        ++received;
        receivedBytes += payloadSize;
        if (latencyUs > targetLatencyUs) {
            ++targetMisses;
        }
        if (latencyUs < minLatencyUs) {
            minLatencyUs = latencyUs;
        }
        if (latencyUs > maxLatencyUs) {
            maxLatencyUs = latencyUs;
        }
        totalLatencyUs += latencyUs;

        const auto bucketLatencyUs = latencyUs < 0 ? 0 : latencyUs;
        for (size_t i = 0; i < latencyBucketUpperUs.size(); ++i) {
            if (bucketLatencyUs <= latencyBucketUpperUs[i]) {
                ++latencyBuckets[i];
                return;
            }
        }
        ++latencyBuckets[latencyBucketUpperUs.size()];
    }

    [[nodiscard]] int64_t averageLatencyUs() const {
        return received == 0
                   ? 0
                   : totalLatencyUs / static_cast<int64_t>(received);
    }

    [[nodiscard]] int64_t minOrZero() const {
        return received == 0 ? 0 : minLatencyUs;
    }

    [[nodiscard]] int64_t maxOrZero() const {
        return received == 0 ? 0 : maxLatencyUs;
    }

    [[nodiscard]] int64_t percentileUs(uint32_t percentile) const {
        if (received == 0) {
            return 0;
        }

        auto rank =
            static_cast<uint32_t>((static_cast<uint64_t>(received) *
                                       percentile +
                                   99U) /
                                  100U);
        if (rank == 0) {
            rank = 1;
        }

        uint32_t cumulative = 0;
        for (size_t i = 0; i < latencyBucketUpperUs.size(); ++i) {
            cumulative += latencyBuckets[i];
            if (cumulative >= rank) {
                return latencyBucketUpperUs[i];
            }
        }
        return maxOrZero();
    }
};

struct RouteDef {
    NodeId source = NodeId::None;
    Topic topic = Topic::None;
    const char *label = "unknown";

    [[nodiscard]] bool matches(const Totem::PubSubBackend::Envelope &envelope)
        const {
        return static_cast<uint8_t>(source) == envelope.header.source &&
               static_cast<uint32_t>(topic) == envelope.header.topic;
    }
};

struct AppStats {
    static constexpr size_t maxRoutes = 4;

    uint32_t publishAttempts = 0;
    uint32_t publishFailures = 0;
    uint32_t publishPoolFull = 0;
    std::array<LatencyStats, maxRoutes> routes{};
    LatencyStats unknownRoute{};

    void recordLatency(size_t routeIndex, int64_t latencyUs,
                       uint16_t payloadSize) {
        if (routeIndex < routes.size()) {
            routes[routeIndex].recordLatency(latencyUs, payloadSize);
            return;
        }
        unknownRoute.recordLatency(latencyUs, payloadSize);
    }
};

struct Consumer {
    const char *name = nullptr;
    AppStats *stats = nullptr;
    const RouteDef *routes = nullptr;
    size_t routeCount = 0;

    static ReturnCode
    callback(void *ctx, const Totem::PubSubBackend::Envelope &envelope) {
        auto *self = static_cast<Consumer *>(ctx);
        return self->handle(envelope);
    }

    ReturnCode handle(const Totem::PubSubBackend::Envelope &envelope) {
        FAIL_IF_UNEXPECTED_FWD(message,
                               envelope.getPayloadAs<PubSubTest::Message>(),
                               "Failed to decode PubSub star test message");
        (void)message;

        const auto &clock = ClockService::get();
        const auto nowUs = clock.nowUs();
        const auto latencyUs =
            !clock.synced() || envelope.header.timestampUs == 0
                ? 0
                : nowUs - static_cast<int64_t>(envelope.header.timestampUs);
        if (stats != nullptr) {
            stats->recordLatency(routeIndex(envelope), latencyUs,
                                 envelope.header.payloadSize);
        }
        return OK();
    }

  private:
    [[nodiscard]] size_t
    routeIndex(const Totem::PubSubBackend::Envelope &envelope) const {
        for (size_t i = 0; i < routeCount; ++i) {
            if (routes[i].matches(envelope)) {
                return i;
            }
        }
        return AppStats::maxRoutes;
    }
};

[[nodiscard]] inline uint32_t ratePerSecond(uint32_t value,
                                            uint32_t elapsedMs) {
    return elapsedMs == 0 ? 0 : (value * 1000U) / elapsedMs;
}

[[nodiscard]] inline Totem::PubSubBackend::Config makePubSubConfig(
    const char *taskName, uint32_t intervalMs = 5,
    Totem::TaskController::Config::CorePreference core =
        Totem::TaskController::Config::CorePreference::any()) {
    return Totem::PubSubBackend::Config{
        .task =
            {
                .name = taskName,
                .priority = 3,
                .core = core,
                .stackSize = 8192,
                .intervalMs = intervalMs,
                .noCatchup = true,
                .useNotify = true,
                .notifyTimeoutMs = intervalMs,
                .autoRestart = false,
            },
        .subscriptionReplayIntervalMs = 1000,
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

inline void reportLatencyStats(const char *name, const char *route,
                               const LatencyStats &stats,
                               uint32_t elapsedMs) {
    _log_i("%s route %s: rx=%" PRIu32 "/s rxBytes=%" PRIu32
           "/s latencyUs[min/avg/p50/p90/p99/max]=%" PRId64 "/%" PRId64
           "/%" PRId64 "/%" PRId64 "/%" PRId64 "/%" PRId64
           " targetMiss=%" PRIu32,
           name, route, ratePerSecond(stats.received, elapsedMs),
           ratePerSecond(stats.receivedBytes, elapsedMs), stats.minOrZero(),
           stats.averageLatencyUs(), stats.percentileUs(50),
           stats.percentileUs(90), stats.percentileUs(99), stats.maxOrZero(),
           stats.targetMisses);
}

inline void reportAppStats(const char *name, const AppStats &period,
                           const RouteDef *routes, size_t routeCount,
                           uint32_t elapsedMs) {
    _log_i("%s app: pub=%" PRIu32 "/s pubFail=%" PRIu32
           " poolFull=%" PRIu32,
           name, ratePerSecond(period.publishAttempts, elapsedMs),
           period.publishFailures, period.publishPoolFull);
    for (size_t i = 0; i < routeCount && i < AppStats::maxRoutes; ++i) {
        reportLatencyStats(name, routes[i].label, period.routes[i], elapsedMs);
    }
    if (period.unknownRoute.received > 0) {
        reportLatencyStats(name, "unknown", period.unknownRoute, elapsedMs);
    }
}

template <class Stats>
inline void reportWireStats(const char *name, const char *transport,
                            const Stats &stats, uint32_t elapsedMs) {
    _log_i("%s %s: txQ=%" PRIu32 "/s txAck=%" PRIu32 "/s txFail=%" PRIu32
           " inFlightFull=%" PRIu32 " txSerDrop=%" PRIu32 " rxQ=%" PRIu32
           "/s rxDrop=%" PRIu32,
           name, transport, ratePerSecond(stats.txQueued, elapsedMs),
           ratePerSecond(stats.txAcked, elapsedMs), stats.txFailed,
           stats.txInFlightFull, stats.txSerializeDropped,
           ratePerSecond(stats.rxQueued, elapsedMs), stats.rxDropped);
}

[[nodiscard]] inline PubSubTest::Message makeMessage(uint32_t value,
                                                     const char *label) {
    PubSubTest::Message msg{};
    msg.flag = (value & 1U) == 0;
    msg.intVal = static_cast<int>(value % 1000U);
    msg.uint32Val = value * 2654435761UL;
    msg.uint16Val = static_cast<uint16_t>(value);
    msg.uint8Val = static_cast<uint8_t>(value);
    std::strncpy(msg.strVal.data(), label, msg.strVal.size() - 1);
    msg.strVal[msg.strVal.size() - 1] = '\0';
    for (size_t i = 0; i < msg.byteArrayVal.size(); ++i) {
        msg.byteArrayVal[i] = static_cast<std::byte>(value + i);
    }
    return msg;
}

} // namespace PubSubStarTest

template <class LowSpeedSpiLink, class HighSpeedSpiLink, class Rs485Link>
struct PubSubStarMasterSetup {
    using MasterPubSub =
        Totem::Data::PubSub::PubSubData<Totem::Data::NodeName::Master>;
    using Transport = MasterPubSub::Transport;
    using LowSpeedSpiTransport =
        Totem::PubSubBackend::Transports::SpiTransport<LowSpeedSpiLink>;
    using HighSpeedSpiTransport =
        Totem::PubSubBackend::Transports::SpiTransport<HighSpeedSpiLink>;
    using Rs485Transport =
        Totem::PubSubBackend::Transports::Rs485Transport<Rs485Link>;
    using LowSpeedSpiDeps =
        Totem::PubSubBackend::Transports::SpiTransportDependencies<
            LowSpeedSpiLink>;
    using HighSpeedSpiDeps =
        Totem::PubSubBackend::Transports::SpiTransportDependencies<
            HighSpeedSpiLink>;
    using Rs485Deps =
        Totem::PubSubBackend::Transports::Rs485TransportDependencies<
            Rs485Link>;

    struct Config {
        uint32_t reportIntervalMs = 1000;
    };

    PubSubStarMasterSetup(Totem::TaskController::IRegistry &taskRegistry,
                          LowSpeedSpiLink &lowSpeedSpiLink,
                          HighSpeedSpiLink &highSpeedSpiLink,
                          Rs485Link &rs485Link,
                          Config config = {})
        : pubSubNode(taskRegistry,
                     static_cast<PubSubStarTest::NodeId>(
                         MasterPubSub::nodeId)),
          config(config),
          lowSpeedSpiTransport(
              makeLowSpeedSpiDeps(pubSubNode, lowSpeedSpiLink)),
          highSpeedSpiTransport(
              makeHighSpeedSpiDeps(pubSubNode, highSpeedSpiLink)),
          rs485Transport(makeRs485Deps(pubSubNode, rs485Link)) {}

    void setup() {
        ABORT_IF_ERR_BEGIN(
            pubSubNode.begin(PubSubStarTest::makePubSubConfig(
                "PubSubStar", 5,
                Totem::TaskController::Config::CorePreference::specific(1))));

        ABORT_IF_ERR_BEGIN(lowSpeedSpiTransport.begin());
        ABORT_IF_ERR(lowSpeedSpiTransport.registerHandler(),
                     "Failed to register low-speed SPI PubSub frame handler");
        ABORT_IF_UNEXPECTED(
            lowSpeedHandle,
            pubSubNode.registerTransport(lowSpeedSpiTransport),
            "Failed to register low-speed SPI PubSub transport");
        (void)lowSpeedHandle;

        ABORT_IF_ERR_BEGIN(highSpeedSpiTransport.begin());
        ABORT_IF_ERR(highSpeedSpiTransport.registerHandler(),
                     "Failed to register high-speed SPI PubSub frame handler");
        ABORT_IF_UNEXPECTED(
            highSpeedHandle,
            pubSubNode.registerTransport(highSpeedSpiTransport),
            "Failed to register high-speed SPI PubSub transport");
        (void)highSpeedHandle;

        ABORT_IF_ERR_BEGIN(rs485Transport.begin());
        ABORT_IF_ERR(rs485Transport.registerHandler(),
                     "Failed to register IO RS485 PubSub frame handler");
        ABORT_IF_UNEXPECTED(
            rs485Handle, pubSubNode.registerTransport(rs485Transport),
            "Failed to register IO RS485 PubSub transport");
        (void)rs485Handle;

        PubSubService::set(pubSubNode);
        _log_i("PubSub star master setup ready");
    }

    ReturnCode work(uint32_t nowMs) {
        reportIfDue(nowMs);
        return OK();
    }

  private:
    [[nodiscard]] static LowSpeedSpiDeps
    makeLowSpeedSpiDeps(PubSubStarTest::PubSubNode &node,
                        LowSpeedSpiLink &link) {
        return LowSpeedSpiDeps{
            .base = PubSubStarTest::makeBaseDeps(
                node, static_cast<uint8_t>(Transport::LowSpeedSPI),
                "PubSub-LowSpeedSPI"),
            .link = link,
        };
    }

    [[nodiscard]] static HighSpeedSpiDeps
    makeHighSpeedSpiDeps(PubSubStarTest::PubSubNode &node,
                         HighSpeedSpiLink &link) {
        return HighSpeedSpiDeps{
            .base = PubSubStarTest::makeBaseDeps(
                node, static_cast<uint8_t>(Transport::HighSpeedSPI),
                "PubSub-HighSpeedSPI"),
            .link = link,
        };
    }

    [[nodiscard]] static Rs485Deps makeRs485Deps(PubSubStarTest::PubSubNode &node,
                                                 Rs485Link &link) {
        return Rs485Deps{
            .base = PubSubStarTest::makeBaseDeps(
                node, static_cast<uint8_t>(Transport::RS485), "PubSub-RS485"),
            .link = link,
        };
    }

    void reportIfDue(uint32_t nowMs) {
        if (config.reportIntervalMs == 0 ||
            nowMs - lastReportAtMs < config.reportIntervalMs) {
            return;
        }

        const auto elapsedMs = lastReportAtMs == 0 ? config.reportIntervalMs
                                                   : nowMs - lastReportAtMs;
        lastReportAtMs = nowMs;

        PubSubStarTest::reportWireStats("PubSub-star-master", "low-spi",
                                        lowSpeedSpiTransport.takeStats(),
                                        elapsedMs);
        PubSubStarTest::reportWireStats("PubSub-star-master", "high-spi",
                                        highSpeedSpiTransport.takeStats(),
                                        elapsedMs);
        PubSubStarTest::reportWireStats("PubSub-star-master", "rs485",
                                        rs485Transport.takeStats(), elapsedMs);
    }

    PubSubStarTest::PubSubNode pubSubNode;
    Config config;
    LowSpeedSpiTransport lowSpeedSpiTransport;
    HighSpeedSpiTransport highSpeedSpiTransport;
    Rs485Transport rs485Transport;
    uint32_t lastReportAtMs = 0;
};

template <class Link> struct PubSubStarSpiEdgeSetup {
    enum class Role : uint8_t {
        Media,
        GPU0,
    };

    using Transport = Totem::Data::PubSub::SPIOnlyTransport;
    using SpiTransport = Totem::PubSubBackend::Transports::SpiTransport<Link>;
    using SpiDeps =
        Totem::PubSubBackend::Transports::SpiTransportDependencies<Link>;
    using TestPool = Totem::PubSubBackend::Pool<PubSubTest::Message, 32>;
    using Consumer = PubSubStarTest::Consumer;
    using AppStats = PubSubStarTest::AppStats;
    using Topic = PubSubStarTest::Topic;

    struct Config {
        const char *consumerName = "PubSub-star-spi";
        const char *publishLabel = "spi star test";
        Topic publishTopic = Topic::None;
        Topic subscribeTopic = Topic::None;
        std::array<PubSubStarTest::RouteDef, AppStats::maxRoutes>
            receiveRoutes{};
        size_t receiveRouteCount = 0;
        uint32_t publishIntervalMs = 10;
        uint32_t publishesPerInterval = 1;
        uint32_t reportIntervalMs = 1000;
        bool publishes = false;
    };

    PubSubStarSpiEdgeSetup(Totem::TaskController::IRegistry &taskRegistry,
                           Link &link, Role role)
        : PubSubStarSpiEdgeSetup(taskRegistry, link, role,
                                 defaultConfig(role)) {}

    PubSubStarSpiEdgeSetup(Totem::TaskController::IRegistry &taskRegistry,
                           Link &link, Role role, Config config)
        : pubSubNode(taskRegistry,
                     static_cast<PubSubStarTest::NodeId>(
                         NodeData::PubSub::nodeId)),
          config(config),
          transport(makeSpiDeps(pubSubNode, link, "PubSub-SPI")),
          messagePool(PubSubService::nextMessageId),
          stats{},
          consumer{this->config.consumerName, &stats,
                   this->config.receiveRoutes.data(),
                   this->config.receiveRouteCount} {}

    void setup() {
        ABORT_IF_ERR_BEGIN(
            pubSubNode.begin(PubSubStarTest::makePubSubConfig("PubSubStar")));
        ABORT_IF_ERR_BEGIN(transport.begin());
        ABORT_IF_ERR(transport.registerHandler(),
                     "Failed to register SPI PubSub frame handler");
        ABORT_IF_UNEXPECTED(
            transportHandle, pubSubNode.registerTransport(transport),
            "Failed to register SPI PubSub transport");
        (void)transportHandle;
        PubSubService::set(pubSubNode);

        if (config.subscribeTopic != Topic::None) {
            ABORT_IF_UNEXPECTED(
                subscriptionHandle,
                pubSubNode.subscribe(
                    "star-sub",
                    {.subscriber = &consumer, .callback = Consumer::callback},
                    config.subscribeTopic),
                "Failed to subscribe PubSub star SPI consumer");
            (void)subscriptionHandle;
        }

        publishStartMs = ::platform::get_time() + warmupMs;
        _log_i("%s setup ready; warming up for %u ms", config.consumerName,
               static_cast<unsigned>(warmupMs));
    }

    ReturnCode work(uint32_t nowMs) {
        reportIfDue(nowMs);
        if (nowMs < publishStartMs || nowMs < nextPublishAtMs) {
            return OK();
        }
        nextPublishAtMs = nowMs + config.publishIntervalMs;
        if (!config.publishes || config.publishTopic == Topic::None) {
            return OK();
        }
        if (!transport.available()) {
            return OK();
        }
        if (!ClockService::get().synced()) {
            return OK();
        }

        for (uint32_t i = 0; i < config.publishesPerInterval; ++i) {
            ++stats.publishAttempts;
            auto publishResult = publishTestMessage();
            if (!publishResult.ok()) {
                ++stats.publishFailures;
                if (publishResult == ERR(CoreError, Overflow)) {
                    ++stats.publishPoolFull;
                }
            }
        }
        return OK();
    }

  private:
    [[nodiscard]] static Config defaultConfig(Role role) {
        if (role == Role::Media) {
            return Config{
                .consumerName = "PubSub-star-media",
                .publishLabel = "media beat to io",
                .publishTopic = Topic::Beat,
                .subscribeTopic = Topic::Power,
                .receiveRoutes =
                    {{
                        {.source = PubSubStarTest::NodeId::InputOutput,
                         .topic = Topic::Power,
                         .label = "io->media power"},
                    }},
                .receiveRouteCount = 1,
                .publishes = true,
            };
        }
        return Config{
            .consumerName = "PubSub-star-gpu0",
            .publishLabel = "gpu0 idle",
            .subscribeTopic = Topic::Power,
            .receiveRoutes =
                {{
                    {.source = PubSubStarTest::NodeId::InputOutput,
                     .topic = Topic::Power,
                     .label = "io->gpu0 power"},
                }},
            .receiveRouteCount = 1,
        };
    }

    [[nodiscard]] static SpiDeps makeSpiDeps(PubSubStarTest::PubSubNode &node,
                                             Link &link, const char *name) {
        return SpiDeps{
            .base = PubSubStarTest::makeBaseDeps(
                node, static_cast<uint8_t>(Transport::SPI), name),
            .link = link,
        };
    }

    ReturnCode publishTestMessage() {
        auto message = PubSubStarTest::makeMessage(++sequence,
                                                   config.publishLabel);

        auto messageIdResult = messagePool.store(message);
        if (!messageIdResult) {
            return messageIdResult.error();
        }
        const auto messageId = *messageIdResult;

        auto envelopeResult =
            Totem::PubSubBackend::Envelope::make<PubSubTest::Message>({
                .owner = static_cast<void *>(&messagePool),
                .topic = config.publishTopic,
                .messageId = messageId,
                .source = static_cast<Totem::PubSubBackend::NodeId>(
                    pubSubNode.nodeId()),
                .getPayloadPtr = TestPool::getPtr,
                .encodePayload = TestPool::encodePayload,
                .release = TestPool::release,
            });
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
        PubSubStarTest::reportAppStats(config.consumerName, period,
                                       config.receiveRoutes.data(),
                                       config.receiveRouteCount, elapsedMs);
        PubSubStarTest::reportWireStats(config.consumerName, "spi",
                                        transport.takeStats(), elapsedMs);
    }

    PubSubStarTest::PubSubNode pubSubNode;
    Config config;
    SpiTransport transport;
    TestPool messagePool;
    AppStats stats{};
    Consumer consumer;
    uint32_t sequence = 0;
    uint32_t publishStartMs = 0;
    uint32_t nextPublishAtMs = 0;
    uint32_t lastReportAtMs = 0;

    static constexpr uint32_t warmupMs = 3000;
};

template <class Link> struct PubSubStarRs485EdgeSetup {
    using InputOutputPubSub = Totem::Data::PubSub::PubSubData<
        Totem::Data::NodeName::InputOutput>;
    using Transport = InputOutputPubSub::Transport;
    using Rs485Transport =
        Totem::PubSubBackend::Transports::Rs485Transport<Link>;
    using Rs485Deps =
        Totem::PubSubBackend::Transports::Rs485TransportDependencies<Link>;
    using TestPool = Totem::PubSubBackend::Pool<PubSubTest::Message, 32>;
    using Consumer = PubSubStarTest::Consumer;
    using AppStats = PubSubStarTest::AppStats;
    using Topic = PubSubStarTest::Topic;

    struct Config {
        const char *consumerName = "PubSub-star-io";
        const char *publishLabel = "io power to spi";
        Topic publishTopic = Topic::Power;
        Topic subscribeTopic = Topic::Beat;
        std::array<PubSubStarTest::RouteDef, AppStats::maxRoutes>
            receiveRoutes{
                {
                    {.source = PubSubStarTest::NodeId::Media,
                     .topic = Topic::Beat,
                     .label = "media->io beat"},
                }};
        size_t receiveRouteCount = 1;
        uint32_t publishIntervalMs = 10;
        uint32_t publishesPerInterval = 1;
        uint32_t reportIntervalMs = 1000;
        bool publishes = true;
    };

    PubSubStarRs485EdgeSetup(Totem::TaskController::IRegistry &taskRegistry,
                             Link &link, Config config = {})
        : pubSubNode(taskRegistry,
                     static_cast<PubSubStarTest::NodeId>(
                         NodeData::PubSub::nodeId)),
          config(config),
          transport(makeRs485Deps(pubSubNode, link, "PubSub-RS485")),
          messagePool(PubSubService::nextMessageId),
          stats{},
          consumer{this->config.consumerName, &stats,
                   this->config.receiveRoutes.data(),
                   this->config.receiveRouteCount} {}

    void setup() {
        ABORT_IF_ERR_BEGIN(
            pubSubNode.begin(PubSubStarTest::makePubSubConfig("PubSubStar")));
        ABORT_IF_ERR_BEGIN(transport.begin());
        ABORT_IF_ERR(transport.registerHandler(),
                     "Failed to register RS485 PubSub frame handler");
        ABORT_IF_UNEXPECTED(
            transportHandle, pubSubNode.registerTransport(transport),
            "Failed to register RS485 PubSub transport");
        (void)transportHandle;
        PubSubService::set(pubSubNode);

        if (config.subscribeTopic != Topic::None) {
            ABORT_IF_UNEXPECTED(
                subscriptionHandle,
                pubSubNode.subscribe(
                    "star-sub",
                    {.subscriber = &consumer, .callback = Consumer::callback},
                    config.subscribeTopic),
                "Failed to subscribe PubSub star RS485 consumer");
            (void)subscriptionHandle;
        }

        publishStartMs = ::platform::get_time() + warmupMs;
        _log_i("%s setup ready; warming up for %u ms", config.consumerName,
               static_cast<unsigned>(warmupMs));
    }

    ReturnCode work(uint32_t nowMs) {
        reportIfDue(nowMs);
        if (nowMs < publishStartMs || nowMs < nextPublishAtMs) {
            return OK();
        }
        nextPublishAtMs = nowMs + config.publishIntervalMs;
        if (!config.publishes || config.publishTopic == Topic::None) {
            return OK();
        }
        if (!transport.available()) {
            return OK();
        }
        if (!ClockService::get().synced()) {
            return OK();
        }

        for (uint32_t i = 0; i < config.publishesPerInterval; ++i) {
            ++stats.publishAttempts;
            auto publishResult = publishTestMessage();
            if (!publishResult.ok()) {
                ++stats.publishFailures;
                if (publishResult == ERR(CoreError, Overflow)) {
                    ++stats.publishPoolFull;
                }
            }
        }
        return OK();
    }

  private:
    [[nodiscard]] static Rs485Deps
    makeRs485Deps(PubSubStarTest::PubSubNode &node, Link &link,
                  const char *name) {
        return Rs485Deps{
            .base = PubSubStarTest::makeBaseDeps(
                node, static_cast<uint8_t>(Transport::RS485), name),
            .link = link,
        };
    }

    ReturnCode publishTestMessage() {
        auto message = PubSubStarTest::makeMessage(++sequence,
                                                   config.publishLabel);

        auto messageIdResult = messagePool.store(message);
        if (!messageIdResult) {
            return messageIdResult.error();
        }
        const auto messageId = *messageIdResult;

        auto envelopeResult =
            Totem::PubSubBackend::Envelope::make<PubSubTest::Message>({
                .owner = static_cast<void *>(&messagePool),
                .topic = config.publishTopic,
                .messageId = messageId,
                .source = static_cast<Totem::PubSubBackend::NodeId>(
                    pubSubNode.nodeId()),
                .getPayloadPtr = TestPool::getPtr,
                .encodePayload = TestPool::encodePayload,
                .release = TestPool::release,
            });
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
        PubSubStarTest::reportAppStats(config.consumerName, period,
                                       config.receiveRoutes.data(),
                                       config.receiveRouteCount, elapsedMs);
        PubSubStarTest::reportWireStats(config.consumerName, "rs485",
                                        transport.takeStats(), elapsedMs);
    }

    PubSubStarTest::PubSubNode pubSubNode;
    Config config;
    Rs485Transport transport;
    TestPool messagePool;
    AppStats stats{};
    Consumer consumer;
    uint32_t sequence = 0;
    uint32_t publishStartMs = 0;
    uint32_t nextPublishAtMs = 0;
    uint32_t lastReportAtMs = 0;

    static constexpr uint32_t warmupMs = 3000;
};
