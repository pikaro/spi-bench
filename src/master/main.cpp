#include "CommandBackend/Facade.hpp"
#include "Data.hpp"
#include "Data/Facade.hpp"
#include "LoggingBackend/Facade.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Facade.hpp"
#include "Monitoring/Facade.hpp"
#include "Platform/Console.hpp"
#include "Platform/platform/PlatformESP32/Base.hpp"
#include "PubSubBackend/Facade.hpp"
#include "PubSubBackend/Interfaces/Config.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/Transports/LocalSharedBusEdgeTransport.hpp"
#include "PubSubBackend/Transports/LocalSharedBusRouterTransport.hpp"
#include "PubSubBackend/Transports/LocalTransport.hpp"
#include "Services/Commands.hpp"
#include "Services/Logging.hpp"
#include "Services/Metrics.hpp"
#include "Services/PubSub.hpp"
#include "Support/CoreCommands.hpp"
#include "TaskControllerRegistry/Facade.hpp"
#include "TestMessage.hpp"
#include "Types/Error.hpp"
#include "master/PubSubTest.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <span>

Totem::MetricsBackend::Backend metricsBackend;

namespace {
struct MetricsBackendBinding {
    MetricsBackendBinding() {
        MetricsService::set(metricsBackend);
        MetricsService::setRegistrar(metricsBackend.registrar());
        MetricsService::setRecorder(metricsBackend.recorder());
    }
} metricsBackendBinding;

} // namespace

Totem::TaskControllerRegistry::Registry taskRegistry;

Totem::LoggingBackend::Aggregator aggregator(taskRegistry);
Totem::LoggingBackend::ConsoleOutput consoleOutput;

Totem::CommandBackend::Controller commandController(taskRegistry);
Totem::CommandBackend::ConsoleTransport consoleSource;
Totem::TaskControllerRegistry::SystemTaskSource systemTaskSource(taskRegistry);

Totem::Monitoring::Monitoring monitoring(taskRegistry);
using PubSubNode = Totem::PubSubBackend::Node;
using Transport = NodeData::PubSub::Transport;
using NodeId = NodeData::PubSub::NodeId;
using Topic = NodeData::PubSub::Topic;
using PeerId = Totem::PubSubBackend::PeerId;
using Harness = PubSubTest::IntegrationHarness;
using Consumer = PubSubTest::Consumer;
using NodeMask = PubSubTest::NodeMask;

PubSubNode pubSubMaster(taskRegistry, NodeId::Master);
PubSubNode pubSubNodeA1(taskRegistry, NodeId::Media);
PubSubNode pubSubNodeA2(taskRegistry, NodeId::GPUNode0);
PubSubNode pubSubNodeA3(taskRegistry, NodeId::GPUNode1);
PubSubNode pubSubNodeA4(taskRegistry, NodeId::GPUNode2);
PubSubNode pubSubBridgeC(taskRegistry, NodeId::InputOutput);
PubSubNode pubSubNodeD(taskRegistry, NodeId::GPUNode3);

constexpr uint32_t spiA1ReadyAfterMs = 5;
constexpr uint32_t spiA2ReadyAfterMs = 10;
constexpr uint32_t spiA3ReadyAfterMs = 15;
constexpr uint32_t spiA4ReadyAfterMs = 20;
constexpr uint32_t spiBridgeCReadyAfterMs = 25;
constexpr uint32_t rs485BridgeCReadyAfterMs = 25;
constexpr uint32_t rs485NodeDReadyAfterMs = 30;

Totem::PubSubBackend::Transports::LocalSharedBusRouterTransport spiRouter({
    .pubSubNode = static_cast<void *>(&pubSubMaster),
    .transportId = static_cast<uint8_t>(Transport::SPI),
    .name = "SPI-Router",
    .sendAckCallback = PubSubNode::ack,
    .availabilityObserver = &pubSubMaster,
    .wakeCallback = PubSubNode::wake,
    .ingressDispatchCallback = PubSubNode::dispatchIngressFrame,
    .ingress = &pubSubMaster.ingress(),
});

Totem::PubSubBackend::Transports::LocalSharedBusLink spiLinkA1("SPI-Link-A1");
Totem::PubSubBackend::Transports::LocalSharedBusLink spiLinkA2("SPI-Link-A2");
Totem::PubSubBackend::Transports::LocalSharedBusLink spiLinkA3("SPI-Link-A3");
Totem::PubSubBackend::Transports::LocalSharedBusLink spiLinkA4("SPI-Link-A4");
Totem::PubSubBackend::Transports::LocalSharedBusLink
    spiLinkBridgeC("SPI-Link-C");

Totem::PubSubBackend::Transports::LocalDMABufferedTransport spiA1({
    .base =
        {
            .pubSubNode = static_cast<void *>(&pubSubNodeA1),
            .transportId = static_cast<uint8_t>(Transport::SPI),
            .name = "SPI-A1",
            .sendAckCallback = PubSubNode::ack,
            .availabilityObserver = &pubSubNodeA1,
            .wakeCallback = PubSubNode::wake,
            .ingressDispatchCallback = PubSubNode::dispatchIngressFrame,
            .ingress = pubSubNodeA1.ingress(),
        },
    .peerId = static_cast<PeerId>(NodeId::Media),
    .readyAfterMs = spiA1ReadyAfterMs,
});

Totem::PubSubBackend::Transports::LocalDMABufferedTransport spiA2({
    .base =
        {
            .pubSubNode = static_cast<void *>(&pubSubNodeA2),
            .transportId = static_cast<uint8_t>(Transport::SPI),
            .name = "SPI-A2",
            .sendAckCallback = PubSubNode::ack,
            .availabilityObserver = &pubSubNodeA2,
            .wakeCallback = PubSubNode::wake,
            .ingressDispatchCallback = PubSubNode::dispatchIngressFrame,
            .ingress = pubSubNodeA2.ingress(),
        },
    .peerId = static_cast<PeerId>(NodeId::GPUNode0),
    .readyAfterMs = spiA2ReadyAfterMs,
});

Totem::PubSubBackend::Transports::LocalDMABufferedTransport spiA3({
    .base =
        {
            .pubSubNode = static_cast<void *>(&pubSubNodeA3),
            .transportId = static_cast<uint8_t>(Transport::SPI),
            .name = "SPI-A3",
            .sendAckCallback = PubSubNode::ack,
            .availabilityObserver = &pubSubNodeA3,
            .wakeCallback = PubSubNode::wake,
            .ingressDispatchCallback = PubSubNode::dispatchIngressFrame,
            .ingress = pubSubNodeA3.ingress(),
        },
    .peerId = static_cast<PeerId>(NodeId::GPUNode1),
    .readyAfterMs = spiA3ReadyAfterMs,
});

Totem::PubSubBackend::Transports::LocalActiveBufferedTransport spiA4({
    .base =
        {
            .pubSubNode = static_cast<void *>(&pubSubNodeA4),
            .transportId = static_cast<uint8_t>(Transport::SPI),
            .name = "SPI-A4",
            .sendAckCallback = PubSubNode::ack,
            .availabilityObserver = &pubSubNodeA4,
            .wakeCallback = PubSubNode::wake,
            .ingressDispatchCallback = PubSubNode::dispatchIngressFrame,
            .ingress = pubSubNodeA4.ingress(),
        },
    .peerId = static_cast<PeerId>(NodeId::GPUNode2),
    .readyAfterMs = spiA4ReadyAfterMs,
});

Totem::PubSubBackend::Transports::LocalDMABufferedTransport spiBridgeC({
    .base =
        {
            .pubSubNode = static_cast<void *>(&pubSubBridgeC),
            .transportId = static_cast<uint8_t>(Transport::SPI),
            .name = "SPI-C",
            .sendAckCallback = PubSubNode::ack,
            .availabilityObserver = &pubSubBridgeC,
            .wakeCallback = PubSubNode::wake,
            .ingressDispatchCallback = PubSubNode::dispatchIngressFrame,
            .ingress = pubSubBridgeC.ingress(),
        },
    .peerId = static_cast<PeerId>(NodeId::InputOutput),
    .readyAfterMs = spiBridgeCReadyAfterMs,
});

Totem::PubSubBackend::Transports::LocalTransport rs485BridgeC({
    .base =
        {
            .pubSubNode = static_cast<void *>(&pubSubBridgeC),
            .transportId = static_cast<uint8_t>(Transport::RS485),
            .name = "RS485-C",
            .sendAckCallback = PubSubNode::ack,
            .availabilityObserver = &pubSubBridgeC,
            .wakeCallback = PubSubNode::wake,
            .ingressDispatchCallback = PubSubNode::dispatchIngressFrame,
            .ingress = pubSubBridgeC.ingress(),
        },
    .readyAfterMs = rs485BridgeCReadyAfterMs,
});

Totem::PubSubBackend::Transports::LocalTransport rs485NodeD({
    .base =
        {
            .pubSubNode = static_cast<void *>(&pubSubNodeD),
            .transportId = static_cast<uint8_t>(Transport::RS485),
            .name = "RS485-D",
            .sendAckCallback = PubSubNode::ack,
            .availabilityObserver = &pubSubNodeD,
            .wakeCallback = PubSubNode::wake,
            .ingressDispatchCallback = PubSubNode::dispatchIngressFrame,
            .ingress = pubSubNodeD.ingress(),
        },
    .readyAfterMs = rs485NodeDReadyAfterMs,
});

using testPool = Totem::PubSubBackend::Pool<Message, 64>;
testPool a1MessagePool{static_cast<void *>(&pubSubNodeA1),
                       PubSubNode::nextMessageId};
testPool cMessagePool{static_cast<void *>(&pubSubBridgeC),
                      PubSubNode::nextMessageId};

Harness pubSubHarness;

Consumer consumerMasterSensor{"MasterSensor", NodeId::Master, pubSubHarness};
Consumer consumerA1Sensor{"A1Sensor", NodeId::Media, pubSubHarness};
Consumer consumerA2Sensor{"A2Sensor", NodeId::GPUNode0, pubSubHarness};
Consumer consumerA3Sensor{"A3Sensor", NodeId::GPUNode1, pubSubHarness};
Consumer consumerA4Sensor{"A4Sensor", NodeId::GPUNode2, pubSubHarness};
Consumer consumerDHeartbeat{"DHeartbeat", NodeId::GPUNode3, pubSubHarness};

constexpr uint32_t targetPropagationLatencyMs = 10;
constexpr uint32_t expectationTimeoutMs = 50;
constexpr uint32_t publishIntervalMs = 10;
constexpr uint32_t harnessWarmupMs = 2000;
constexpr uint32_t harnessReportIntervalMs = 1000;

constexpr NodeMask sensorRecipients = PubSubTest::nodeMask(NodeId::Master) |
                                      PubSubTest::nodeMask(NodeId::Media) |
                                      PubSubTest::nodeMask(NodeId::GPUNode0) |
                                      PubSubTest::nodeMask(NodeId::GPUNode1) |
                                      PubSubTest::nodeMask(NodeId::GPUNode2);

constexpr NodeMask heartbeatRecipients = PubSubTest::nodeMask(NodeId::GPUNode3);

const auto a1PoolProbe = Harness::makePoolProbe("A1 pool", a1MessagePool);
const auto cPoolProbe = Harness::makePoolProbe("C pool", cMessagePool);

const auto sensorTransportProbes = std::to_array<Harness::TransportProbe>({
    Harness::makeTransportProbe("SPI-C edge egress", spiBridgeC),
    Harness::makeTransportProbe("SPI router egress", spiRouter),
});

const auto heartbeatTransportProbes = std::to_array<Harness::TransportProbe>({
    Harness::makeTransportProbe("SPI-A1 edge egress", spiA1),
    Harness::makeTransportProbe("SPI router egress", spiRouter),
    Harness::makeTransportProbe("SPI-C edge egress", spiBridgeC),
    Harness::makeTransportProbe("RS485-C egress", rs485BridgeC),
});

[[nodiscard]] Totem::PubSubBackend::Config
makePubSubConfig(const char *taskName, uint32_t intervalMs = 500,
                 uint32_t notifyTimeoutMs = 500) {
    return Totem::PubSubBackend::Config{
        .task =
            {
                .name = taskName,
                .priority = 3,
                .stackSize = 8192,
                .intervalMs = intervalMs,
                .noCatchup = true,
                .useNotify = true,
                .notifyTimeoutMs = notifyTimeoutMs,
                .autoRestart = false,
            },
    };
}

// NOLINTNEXTLINE(readability-function-size)
void setup() {
    ABORT_IF_ERR_BEGIN(::platform::Console::init());

    ABORT_IF_ERR_BEGIN(taskRegistry.begin());

    ABORT_IF_ERR_BEGIN(consoleOutput.begin());

    ABORT_IF_ERR_BEGIN(commandController.begin());
    ABORT_IF_ERR(commandController.addTransport(consoleSource),
                 "Failed to add console transport to command controller");
    CommandRegistrarService::set(commandController.registrar());

    ABORT_IF_ERR_BEGIN(metricsBackend.begin());

    ABORT_IF_ERR(register_core_commands(),
                 "Failed to register core commands to command controller");

    ABORT_IF_ERR_BEGIN(aggregator.begin());
    ABORT_IF_ERR(aggregator.addSink(consoleOutput),
                 "Failed to add console sink to aggregator");

    LoggingService::set(aggregator);

    ABORT_IF_ERR_BEGIN(monitoring.begin());

    ABORT_IF_ERR_BEGIN(
        pubSubMaster.begin(makePubSubConfig("PubSubMaster", 10, 5)));
    ABORT_IF_ERR_BEGIN(pubSubNodeA1.begin(makePubSubConfig("PubSubA1")));
    ABORT_IF_ERR_BEGIN(pubSubNodeA2.begin(makePubSubConfig("PubSubA2")));
    ABORT_IF_ERR_BEGIN(pubSubNodeA3.begin(makePubSubConfig("PubSubA3")));
    ABORT_IF_ERR_BEGIN(pubSubNodeA4.begin(makePubSubConfig("PubSubA4")));
    ABORT_IF_ERR_BEGIN(pubSubBridgeC.begin(makePubSubConfig("PubSubBridgeC")));
    ABORT_IF_ERR_BEGIN(pubSubNodeD.begin(makePubSubConfig("PubSubD")));

    ABORT_IF_ERR_BEGIN(spiRouter.begin());
    ABORT_IF_ERR_BEGIN(spiA1.begin());
    ABORT_IF_ERR_BEGIN(spiA2.begin());
    ABORT_IF_ERR_BEGIN(spiA3.begin());
    ABORT_IF_ERR_BEGIN(spiA4.begin());
    ABORT_IF_ERR_BEGIN(spiBridgeC.begin());
    ABORT_IF_ERR_BEGIN(rs485BridgeC.begin());
    ABORT_IF_ERR_BEGIN(rs485NodeD.begin());

    ABORT_IF_ERR(spiA1.addLink(spiLinkA1), "Failed to link SPI peer A1");
    ABORT_IF_ERR(spiA2.addLink(spiLinkA2), "Failed to link SPI peer A2");
    ABORT_IF_ERR(spiA3.addLink(spiLinkA3), "Failed to link SPI peer A3");
    ABORT_IF_ERR(spiA4.addLink(spiLinkA4), "Failed to link SPI peer A4");
    ABORT_IF_ERR(spiBridgeC.addLink(spiLinkBridgeC),
                 "Failed to link SPI bridge peer C");

    ABORT_IF_ERR(spiRouter.addPeer(spiA1), "Failed to attach SPI peer A1");
    ABORT_IF_ERR(spiRouter.addPeer(spiA2), "Failed to attach SPI peer A2");
    ABORT_IF_ERR(spiRouter.addPeer(spiA3), "Failed to attach SPI peer A3");
    ABORT_IF_ERR(spiRouter.addPeer(spiA4), "Failed to attach SPI peer A4");
    ABORT_IF_ERR(spiRouter.addPeer(spiBridgeC),
                 "Failed to attach SPI bridge peer C");
    ABORT_IF_ERR(rs485BridgeC.addLink(rs485NodeD),
                 "Failed to link RS485 bridge and node D");

    ABORT_IF_UNEXPECTED(masterSpiHandle,
                        pubSubMaster.registerTransport(spiRouter),
                        "Failed to register SPI router with master node");
    ABORT_IF_UNEXPECTED(a1SpiHandle, pubSubNodeA1.registerTransport(spiA1),
                        "Failed to register SPI edge A1");
    ABORT_IF_UNEXPECTED(a2SpiHandle, pubSubNodeA2.registerTransport(spiA2),
                        "Failed to register SPI edge A2");
    ABORT_IF_UNEXPECTED(a3SpiHandle, pubSubNodeA3.registerTransport(spiA3),
                        "Failed to register SPI edge A3");
    ABORT_IF_UNEXPECTED(a4SpiHandle, pubSubNodeA4.registerTransport(spiA4),
                        "Failed to register SPI edge A4");
    ABORT_IF_UNEXPECTED(bridgeSpiHandle,
                        pubSubBridgeC.registerTransport(spiBridgeC),
                        "Failed to register SPI edge C");
    ABORT_IF_UNEXPECTED(bridgeRs485Handle,
                        pubSubBridgeC.registerTransport(rs485BridgeC),
                        "Failed to register RS485 transport on bridge C");
    ABORT_IF_UNEXPECTED(nodeDRs485Handle,
                        pubSubNodeD.registerTransport(rs485NodeD),
                        "Failed to register RS485 transport on node D");

    PubSubService::set(pubSubMaster);

    (void)masterSpiHandle;
    (void)a1SpiHandle;
    (void)a2SpiHandle;
    (void)a3SpiHandle;
    (void)a4SpiHandle;
    (void)bridgeSpiHandle;
    (void)bridgeRs485Handle;
    (void)nodeDRs485Handle;

    ABORT_IF_UNEXPECTED(
        masterSensorSub,
        pubSubMaster.subscribe("msens",
                               {.subscriber = &consumerMasterSensor,
                                .callback = Consumer::callback},
                               Topic::Sensor),
        "Failed to subscribe master to Sensor");
    ABORT_IF_UNEXPECTED(a1SensorSub,
                        pubSubNodeA1.subscribe("a1sens",
                                               {.subscriber = &consumerA1Sensor,
                                                .callback = Consumer::callback},
                                               Topic::Sensor),
                        "Failed to subscribe A1 to Sensor");
    ABORT_IF_UNEXPECTED(a2SensorSub,
                        pubSubNodeA2.subscribe("a2sens",
                                               {.subscriber = &consumerA2Sensor,
                                                .callback = Consumer::callback},
                                               Topic::Sensor),
                        "Failed to subscribe A2 to Sensor");
    ABORT_IF_UNEXPECTED(a3SensorSub,
                        pubSubNodeA3.subscribe("a3sens",
                                               {.subscriber = &consumerA3Sensor,
                                                .callback = Consumer::callback},
                                               Topic::Sensor),
                        "Failed to subscribe A3 to Sensor");
    ABORT_IF_UNEXPECTED(a4SensorSub,
                        pubSubNodeA4.subscribe("a4sns",
                                               {.subscriber = &consumerA4Sensor,
                                                .callback = Consumer::callback},
                                               Topic::Sensor),
                        "Failed to subscribe A4 to Sensor");
    ABORT_IF_UNEXPECTED(
        dHeartbeatSub,
        pubSubNodeD.subscribe(
            "dbeat",
            {.subscriber = &consumerDHeartbeat, .callback = Consumer::callback},
            Topic::Heartbeat),
        "Failed to subscribe node D to Heartbeat");

    (void)masterSensorSub;
    (void)a1SensorSub;
    (void)a2SensorSub;
    (void)a3SensorSub;
    (void)a4SensorSub;
    (void)dHeartbeatSub;
    _log_i("Setup complete");
}

extern "C" {
void app_main(void);
}

::platform::Tick lastWakeTime;
uint32_t publishCycle = 0;

ReturnCode
publishTestMessage(PubSubNode &node, testPool &pool, Topic topic,
                   const char *label, NodeMask recipients,
                   const Harness::PoolProbe &poolProbe,
                   std::span<const Harness::TransportProbe> transportProbes) {
    auto message = PubSubTest::makeTestMessage();
    std::strncpy(message.strVal.data(), label, message.strVal.size() - 1);
    message.strVal[message.strVal.size() - 1] = '\0';

    ABORT_IF_ERR(pubSubHarness.recordPublicationAttempt(),
                 "Failed to record publication attempt");
    ABORT_IF_UNEXPECTED(
        messageId, pool.store(message),
        "Failed to allocate message from pool for topic " SV_FMT,
        SV_ARG(magic_enum::enum_name(topic)));
    auto envelopeDef = Totem::PubSubBackend::EnvelopeDef{
        .owner = static_cast<void *>(&pool),
        .topic = topic,
        .messageId = messageId,
        .source = static_cast<Totem::PubSubBackend::NodeId>(node.nodeId()),
        .getPayloadPtr = testPool::getPtr,
        .encodePayload = testPool::encodePayload,
        .release = testPool::release,
    };
    ABORT_IF_UNEXPECTED(
        envelope, Totem::PubSubBackend::Envelope::make<Message>(envelopeDef),
        "Failed to create test envelope for topic " SV_FMT,
        SV_ARG(magic_enum::enum_name(topic)));

    auto nowMs = ::platform::get_time();
    auto expectationResult =
        pubSubHarness.expect(envelope.header, message, recipients, poolProbe,
                             transportProbes, nowMs);
    if (!expectationResult.ok()) {
        auto releaseResult = pool.release(envelope);
        FAIL_IF_ERR_FWD(releaseResult,
                        "Failed to release pool message after expectation "
                        "setup failure");
        return expectationResult;
    }

    auto publishResult = node.publish(envelope);
    if (!publishResult.ok()) {
        pubSubHarness.cancel(envelope.header);
        auto releaseResult = pool.release(envelope);
        FAIL_IF_ERR_FWD(releaseResult,
                        "Failed to release pool message after publish "
                        "failure");
        return publishResult;
    }
    return OK();
}

void app_main() {
    setup();
    const auto publishStartMs = ::platform::get_time() + harnessWarmupMs;
    _log_i("PubSubTest: warming up subscriptions for %u ms before publishing",
           static_cast<unsigned>(harnessWarmupMs));
    for (;;) {
        if (auto reapResult = taskRegistry.reap(); !reapResult.ok()) {
            _log_e("Error during task registry reap: " ERR_FMT,
                   ERR_ARG(reapResult));
        }

        const auto nowMs = ::platform::get_time();
        pubSubHarness.poll(nowMs, expectationTimeoutMs, harnessReportIntervalMs,
                           targetPropagationLatencyMs);

        if (nowMs < publishStartMs) {
            ::platform::delay_until(&lastWakeTime, publishIntervalMs);
            continue;
        }

        auto sensorResult = publishTestMessage(
            pubSubBridgeC, cMessagePool, Topic::Sensor, "Sensor from C",
            sensorRecipients, cPoolProbe, sensorTransportProbes);
        if (!sensorResult.ok()) {
            _log_e("Failed to publish sensor message: " ERR_FMT,
                   ERR_ARG(sensorResult));
        }

        if ((publishCycle++ % 2U) == 0) {
            auto heartbeatResult = publishTestMessage(
                pubSubNodeA1, a1MessagePool, Topic::Heartbeat, "Beat from A1",
                heartbeatRecipients, a1PoolProbe, heartbeatTransportProbes);
            if (!heartbeatResult.ok()) {
                _log_e("Failed to publish heartbeat message: " ERR_FMT,
                       ERR_ARG(heartbeatResult));
            }
        }

        ::platform::delay_until(&lastWakeTime, publishIntervalMs);
    }
}
