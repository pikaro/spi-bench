#pragma once

#include "Base/HasMutex.hpp"
#include "Data.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "PubSubBackend/Facade.hpp"
#include "PubSubBackend/Interfaces/Config.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/Interfaces/Wire.hpp"
#include "PubSubBackend/Transports/BaseTransport.hpp"
#include "PubSubBackend/Transports/LocalSharedBusEdgeTransport.hpp"
#include "PubSubBackend/Transports/LocalSharedBusRouterTransport.hpp"
#include "PubSubBackend/Transports/LocalTransport.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Services/PubSub.hpp"
#include "Setups/PubSubTestMessage.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace PubSubTest {

using Topic = Totem::Data::PubSub::Topic;
using NodeId = Totem::Data::PubSub::NodeId;
using MessageId = Totem::PubSubBackend::detail::MessageId;
using Header = Totem::PubSubBackend::Header;

using NodeMask = uint32_t;

[[nodiscard]] constexpr NodeMask nodeMask(NodeId nodeId) {
    return static_cast<NodeMask>(nodeId);
}

[[nodiscard]] inline bool messageEquals(const Message &lhs,
                                        const Message &rhs) {
    return lhs.flag == rhs.flag && lhs.intVal == rhs.intVal &&
           lhs.uint32Val == rhs.uint32Val && lhs.uint16Val == rhs.uint16Val &&
           lhs.uint8Val == rhs.uint8Val && lhs.strVal == rhs.strVal &&
           lhs.byteArrayVal == rhs.byteArrayVal;
}

inline Message makeTestMessage() {
    static uint32_t sequence = 0;
    const auto value = ++sequence;

    Message msg{};
    msg.flag = (value & 1U) == 0;
    msg.intVal = static_cast<int>(value % 1000U);
    msg.uint32Val = value * 2654435761UL;
    msg.uint16Val = static_cast<uint16_t>(value);
    msg.uint8Val = static_cast<uint8_t>(value);
    std::strncpy(msg.strVal.data(), "Hello, PubSub!", msg.strVal.size() - 1);
    msg.strVal[msg.strVal.size() - 1] = '\0';
    for (size_t i = 0; i < msg.byteArrayVal.size(); ++i) {
        msg.byteArrayVal[i] = static_cast<std::byte>(value + i);
    }
    return msg;
}

class IntegrationHarness : public HasMutex<IntegrationHarness> {
  public:
    static constexpr const char *name = "PubSubTest::IntegrationHarness";

    struct Stats {
        uint32_t publishAttempts = 0;
        uint32_t publishFailures = 0;
        uint32_t expectationsInstalled = 0;
        uint32_t expectationOverflow = 0;
        uint32_t completions = 0;
        uint32_t maxReceiptLatencyMs = 0;
        uint32_t maxCompletionLatencyMs = 0;
        uint32_t latencyTargetMisses = 0;
        uint32_t unexpectedMessages = 0;
        uint32_t unexpectedRecipients = 0;
        uint32_t duplicateReceipts = 0;
        uint32_t payloadMismatches = 0;
        uint32_t receiptTimeouts = 0;
        uint32_t poolFreeTimeouts = 0;
        uint32_t egressFreeTimeouts = 0;
    };

    struct PoolProbe {
        const char *name = nullptr;
        const void *ctx = nullptr;
        bool (*wasFreed)(const void *ctx, MessageId messageId) = nullptr;

        [[nodiscard]] bool valid() const {
            return name != nullptr && ctx != nullptr && wasFreed != nullptr;
        }
    };

    struct TransportProbe {
        const char *name = nullptr;
        const void *ctx = nullptr;
        bool (*wasFreed)(const void *ctx, const Header &header) = nullptr;

        [[nodiscard]] bool valid() const {
            return name != nullptr && ctx != nullptr && wasFreed != nullptr;
        }
    };

    template <typename Pool>
    [[nodiscard]] static PoolProbe makePoolProbe(const char *probeName,
                                                 const Pool &pool) {
        return PoolProbe{
            .name = probeName,
            .ctx = &pool,
            .wasFreed = [](const void *ctx, MessageId messageId) -> bool {
                auto *typedPool = static_cast<const Pool *>(ctx);
                return typedPool->wasFreed(messageId);
            },
        };
    }

    template <typename Transport>
    [[nodiscard]] static TransportProbe
    makeTransportProbe(const char *probeName, const Transport &transport) {
        return TransportProbe{
            .name = probeName,
            .ctx = &transport,
            .wasFreed = [](const void *ctx, const Header &header) -> bool {
                auto *typedTransport = static_cast<const Transport *>(ctx);
                return typedTransport->wasFrameFreed(header);
            },
        };
    }

    static constexpr size_t maxExpectations = 32;
    static constexpr size_t maxTransportProbes = 4;

    ReturnCode expect(const Header &header, const Message &message,
                      NodeMask expectedRecipients, const PoolProbe &poolProbe,
                      std::span<const TransportProbe> transportProbes,
                      uint32_t nowMs) {
        auto guard = _mutexGuard(mutexTimeoutMs);
        FAIL_IF_NOT(guard.acquired(), ERR(Timeout),
                    "Timed out locking PubSub test harness for expect");
        FAIL_IF_NOT(poolProbe.valid(), ERR(InvalidArgument),
                    "Expectation requires a valid pool probe");
        FAIL_IF(expectedRecipients == 0, ERR(InvalidArgument),
                "Expectation requires at least one target recipient");
        FAIL_IF(transportProbes.size() > maxTransportProbes,
                ERR(InvalidArgument),
                "Expectation exceeds maximum transport probe count");

        auto *slot = _findFreeExpectation();
        if (slot == nullptr) {
            ++_stats.expectationOverflow;
            return ERR(Overflow);
        }

        slot->occupied = true;
        slot->header = header;
        slot->message = message;
        slot->expectedRecipients = expectedRecipients;
        slot->receivedRecipients = 0;
        slot->poolProbe = poolProbe;
        slot->transportProbeCount = transportProbes.size();
        slot->createdAtMs = nowMs;
        for (size_t i = 0; i < maxTransportProbes; ++i) {
            slot->transportProbes[i] = {};
        }
        for (size_t i = 0; i < transportProbes.size(); ++i) {
            slot->transportProbes[i] = transportProbes[i];
        }

        ++_stats.expectationsInstalled;
        return OK();
    }

    void cancel(const Header &header) {
        auto guard = _mutexGuard(mutexTimeoutMs);
        if (!guard.acquired()) {
            return;
        }
        if (auto *expectation = _findExpectation(header)) {
            expectation->occupied = false;
        }
        ++_stats.publishFailures;
    }

    ReturnCode recordPublicationAttempt() {
        auto guard = _mutexGuard(mutexTimeoutMs);
        FAIL_IF_NOT(guard.acquired(), ERR(Timeout),
                    "Timed out locking PubSub test harness for attempt");
        ++_stats.publishAttempts;
        return OK();
    }

    ReturnCode recordReceipt(NodeId recipient,
                             const Totem::PubSubBackend::Envelope &envelope) {
        FAIL_IF_UNEXPECTED_FWD(message, envelope.getPayloadAs<Message>(),
                               "Failed to decode message payload");
        auto guard = _mutexGuard(mutexTimeoutMs);
        FAIL_IF_NOT(guard.acquired(), ERR(Timeout),
                    "Timed out locking PubSub test harness for receipt");

        auto *expectation = _findExpectation(envelope.header);
        if (expectation == nullptr) {
            ++_stats.unexpectedMessages;
            _log_e("PubSubTest: unexpected message for source %u "
                   "messageId %u topic " SV_FMT,
                   static_cast<unsigned>(envelope.header.source),
                   envelope.header.messageId,
                   MAGIC_SV_ARG(Totem::Data::PubSub::Topic,
                                envelope.header.topic));
            return ERR(NotFound);
        }

        const auto recipientBit = nodeMask(recipient);
        if ((expectation->expectedRecipients & recipientBit) == 0) {
            ++_stats.unexpectedRecipients;
            _log_e("PubSubTest: unexpected recipient %u for source %u "
                   "messageId %u topic " SV_FMT,
                   static_cast<unsigned>(recipient),
                   static_cast<unsigned>(envelope.header.source),
                   envelope.header.messageId,
                   MAGIC_SV_ARG(Totem::Data::PubSub::Topic,
                                envelope.header.topic));
            return ERR(InvalidState);
        }

        if ((expectation->receivedRecipients & recipientBit) != 0) {
            ++_stats.duplicateReceipts;
            _log_e("PubSubTest: duplicate receipt at node %u for source %u "
                   "messageId %u topic " SV_FMT,
                   static_cast<unsigned>(recipient),
                   static_cast<unsigned>(envelope.header.source),
                   envelope.header.messageId,
                   MAGIC_SV_ARG(Totem::Data::PubSub::Topic,
                                envelope.header.topic));
            return ERR(AlreadyExists);
        }

        if (!messageEquals(message, expectation->message)) {
            ++_stats.payloadMismatches;
            _log_e("PubSubTest: payload mismatch at node %u for source %u "
                   "messageId %u topic " SV_FMT,
                   static_cast<unsigned>(recipient),
                   static_cast<unsigned>(envelope.header.source),
                   envelope.header.messageId,
                   MAGIC_SV_ARG(Totem::Data::PubSub::Topic,
                                envelope.header.topic));
            return ERR(InvalidState);
        }

        const auto nowMs = ::platform::get_time();
        const auto receiptLatencyMs = nowMs - expectation->createdAtMs;
        if (receiptLatencyMs > _stats.maxReceiptLatencyMs) {
            _stats.maxReceiptLatencyMs = receiptLatencyMs;
        }
        if (nowMs > expectation->lastReceiptAtMs) {
            expectation->lastReceiptAtMs = nowMs;
        }
        expectation->receivedRecipients |= recipientBit;
        return OK();
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void poll(uint32_t nowMs, uint32_t timeoutMs, uint32_t reportIntervalMs,
              uint32_t targetLatencyMs = 0) {
        std::array<TimeoutEvent, maxExpectations> timeoutEvents{};
        size_t timeoutEventCount = 0;
        ReportSnapshot report{};

        {
            auto guard = _mutexGuard(mutexTimeoutMs);
            if (!guard.acquired()) {
                return;
            }
            _evaluateExpectations(nowMs, timeoutMs, targetLatencyMs,
                                  timeoutEvents, timeoutEventCount);
            if (reportIntervalMs != 0 &&
                (nowMs - _lastReportAtMs) >= reportIntervalMs) {
                _lastReportAtMs = nowMs;
                report.emit = true;
                report.stats = _stats;
                report.pending = _pendingCount();
            }
        }

        for (size_t i = 0; i < timeoutEventCount; ++i) {
            const auto &event = timeoutEvents[i];
            if (event.missingRecipients != 0) {
                _log_e("PubSubTest: receipt timeout for source %u messageId %u "
                       "topic " SV_FMT " missing recipients mask 0x%08" PRIx32,
                       static_cast<unsigned>(event.header.source),
                       event.header.messageId,
                       MAGIC_SV_ARG(Totem::Data::PubSub::Topic,
                                    event.header.topic),
                       event.missingRecipients);
            }
            if (event.poolName != nullptr) {
                _log_e("PubSubTest: pool release timeout for %s source %u "
                       "messageId %u topic " SV_FMT,
                       event.poolName,
                       static_cast<unsigned>(event.header.source),
                       event.header.messageId,
                       MAGIC_SV_ARG(Totem::Data::PubSub::Topic,
                                    event.header.topic));
            }
            for (size_t j = 0; j < event.egressTimeoutCount; ++j) {
                _log_e("PubSubTest: egress release timeout for %s source %u "
                       "messageId %u topic " SV_FMT,
                       event.egressNames[j],
                       static_cast<unsigned>(event.header.source),
                       event.header.messageId,
                       MAGIC_SV_ARG(Totem::Data::PubSub::Topic,
                                    event.header.topic));
            }
        }

        if (report.emit) {
            _log_i(
                "PubSubTest: attempts=%" PRIu32 " pending=%zu complete=%" PRIu32
                " maxReceiptMs=%" PRIu32 " maxCompleteMs=%" PRIu32
                " targetMiss=%" PRIu32 " publishFail=%" PRIu32
                " unexpected=%" PRIu32 " badRecipient=%" PRIu32
                " duplicate=%" PRIu32 " mismatch=%" PRIu32
                " receiptTimeout=%" PRIu32 " poolTimeout=%" PRIu32
                " egressTimeout=%" PRIu32,
                report.stats.publishAttempts, report.pending,
                report.stats.completions, report.stats.maxReceiptLatencyMs,
                report.stats.maxCompletionLatencyMs,
                report.stats.latencyTargetMisses, report.stats.publishFailures,
                report.stats.unexpectedMessages,
                report.stats.unexpectedRecipients,
                report.stats.duplicateReceipts, report.stats.payloadMismatches,
                report.stats.receiptTimeouts, report.stats.poolFreeTimeouts,
                report.stats.egressFreeTimeouts);
        }
    }

    [[nodiscard]] size_t pendingCount() const {
        auto guard = _mutexGuard(mutexTimeoutMs);
        return guard.acquired() ? _pendingCount() : 0;
    }

    [[nodiscard]] Stats stats() const {
        auto guard = _mutexGuard(mutexTimeoutMs);
        return guard.acquired() ? _stats : Stats{};
    }

  private:
    [[nodiscard]] size_t _pendingCount() const {
        size_t pending = 0;
        for (const auto &expectation : _expectations) {
            if (expectation.occupied) {
                ++pending;
            }
        }
        return pending;
    }
    struct Expectation {
        bool occupied = false;
        Header header{};
        Message message{};
        NodeMask expectedRecipients = 0;
        NodeMask receivedRecipients = 0;
        PoolProbe poolProbe{};
        std::array<TransportProbe, maxTransportProbes> transportProbes{};
        size_t transportProbeCount = 0;
        uint32_t createdAtMs = 0;
        uint32_t lastReceiptAtMs = 0;
    };

    struct TimeoutEvent {
        Header header{};
        NodeMask missingRecipients = 0;
        const char *poolName = nullptr;
        std::array<const char *, maxTransportProbes> egressNames{};
        size_t egressTimeoutCount = 0;
    };

    struct ReportSnapshot {
        bool emit = false;
        Stats stats{};
        size_t pending = 0;
    };

    [[nodiscard]] static bool
    _allExpectedRecipientsObserved(const Expectation &expectation) {
        return expectation.receivedRecipients == expectation.expectedRecipients;
    }

    [[nodiscard]] static bool
    _allTransportBuffersFreed(const Expectation &expectation) {
        for (size_t i = 0; i < expectation.transportProbeCount; ++i) {
            const auto &probe = expectation.transportProbes[i];
            if (!probe.valid()) {
                return false;
            }
            if (!probe.wasFreed(probe.ctx, expectation.header)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static NodeMask
    _missingRecipients(const Expectation &expectation) {
        return expectation.expectedRecipients & ~expectation.receivedRecipients;
    }

    Expectation *_findFreeExpectation() {
        for (auto &expectation : _expectations) {
            if (!expectation.occupied) {
                return &expectation;
            }
        }
        return nullptr;
    }

    Expectation *_findExpectation(const Header &header) {
        for (auto &expectation : _expectations) {
            if (expectation.occupied &&
                expectation.header.topic == header.topic &&
                expectation.header.source == header.source &&
                expectation.header.messageId == header.messageId) {
                return &expectation;
            }
        }
        return nullptr;
    }

    void _complete(Expectation &expectation, uint32_t targetLatencyMs) {
        const auto completionLatencyMs =
            expectation.lastReceiptAtMs - expectation.createdAtMs;
        if (completionLatencyMs > _stats.maxCompletionLatencyMs) {
            _stats.maxCompletionLatencyMs = completionLatencyMs;
        }
        if (targetLatencyMs > 0 && completionLatencyMs > targetLatencyMs) {
            ++_stats.latencyTargetMisses;
        }
        expectation.occupied = false;
        ++_stats.completions;
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters,readability-function-cognitive-complexity)
    void _evaluateExpectations(uint32_t nowMs, uint32_t timeoutMs,
                               uint32_t targetLatencyMs,
                               std::span<TimeoutEvent> timeoutEvents,
                               size_t &timeoutEventCount) {
        for (auto &expectation : _expectations) {
            if (!expectation.occupied) {
                continue;
            }

            const auto receivedAll =
                _allExpectedRecipientsObserved(expectation);
            const auto poolFreed = expectation.poolProbe.wasFreed(
                expectation.poolProbe.ctx, expectation.header.messageId);
            const auto egressFreed = _allTransportBuffersFreed(expectation);

            if (receivedAll && poolFreed && egressFreed) {
                _complete(expectation, targetLatencyMs);
                continue;
            }

            if ((nowMs - expectation.createdAtMs) < timeoutMs) {
                continue;
            }

            TimeoutEvent *event = nullptr;
            if (timeoutEventCount < timeoutEvents.size()) {
                event = &timeoutEvents[timeoutEventCount++];
                event->header = expectation.header;
            }

            if (!receivedAll) {
                ++_stats.receiptTimeouts;
                if (event != nullptr) {
                    event->missingRecipients = _missingRecipients(expectation);
                }
            }

            if (!poolFreed) {
                ++_stats.poolFreeTimeouts;
                if (event != nullptr) {
                    event->poolName = expectation.poolProbe.name;
                }
            }

            if (!egressFreed) {
                for (size_t i = 0; i < expectation.transportProbeCount; ++i) {
                    const auto &probe = expectation.transportProbes[i];
                    if (!probe.wasFreed(probe.ctx, expectation.header)) {
                        ++_stats.egressFreeTimeouts;
                        if (event != nullptr && event->egressTimeoutCount <
                                                    event->egressNames.size()) {
                            event->egressNames[event->egressTimeoutCount++] =
                                probe.name;
                        }
                    }
                }
            }

            expectation.occupied = false;
        }
    }

    std::array<Expectation, maxExpectations> _expectations{};
    Stats _stats{};
    uint32_t _lastReportAtMs = 0;

    static constexpr uint32_t mutexTimeoutMs = 10;
};

class Consumer {
  public:
    Consumer(const char *name, NodeId nodeId, IntegrationHarness &harness)
        : _name(name), _nodeId(nodeId), _harness(harness) {}

    static ReturnCode callback(void *ctx,
                               const Totem::PubSubBackend::Envelope &envelope) {
        auto *self = static_cast<Consumer *>(ctx);
        return self->handleMessage(envelope);
    }

    ReturnCode
    handleMessage(const Totem::PubSubBackend::Envelope &envelope) const {
        auto ret = _harness.recordReceipt(_nodeId, envelope);
        if (!ret.ok()) {
            _log_e("PubSubTest: %s failed to acknowledge message: " ERR_FMT,
                   _name, ERR_ARG(ret));
            return OK();
        }
        return OK();
    }

  private:
    const char *_name;
    NodeId _nodeId;
    IntegrationHarness &_harness;
};

} // namespace PubSubTest

constexpr uint32_t spiA1ReadyAfterMs = 5;
constexpr uint32_t spiA2ReadyAfterMs = 10;
constexpr uint32_t spiA3ReadyAfterMs = 15;
constexpr uint32_t spiA4ReadyAfterMs = 20;
constexpr uint32_t spiBridgeCReadyAfterMs = 25;
constexpr uint32_t rs485BridgeCReadyAfterMs = 25;
constexpr uint32_t rs485NodeDReadyAfterMs = 30;

constexpr uint32_t targetPropagationLatencyMs = 10;
constexpr uint32_t expectationTimeoutMs = 50;
constexpr uint32_t publishIntervalMs = 10;
constexpr uint32_t harnessWarmupMs = 2000;
constexpr uint32_t harnessReportIntervalMs = 1000;

constexpr PubSubTest::NodeMask sensorRecipients =
    PubSubTest::nodeMask(PubSubTest::NodeId::Master) |
    PubSubTest::nodeMask(PubSubTest::NodeId::GPUNode0) |
    PubSubTest::nodeMask(PubSubTest::NodeId::GPUNode1) |
    PubSubTest::nodeMask(PubSubTest::NodeId::GPUNode2) |
    PubSubTest::nodeMask(PubSubTest::NodeId::GPUNode3);

constexpr PubSubTest::NodeMask heartbeatRecipients =
    PubSubTest::nodeMask(PubSubTest::NodeId::InputOutput);

struct PubSubTestSetup {
    using PubSubNode = Totem::PubSubBackend::Node;
    using Transport = NodeData::PubSub::Transport;
    using NodeId = NodeData::PubSub::NodeId;
    using Topic = NodeData::PubSub::Topic;
    using PeerId = Totem::PubSubBackend::PeerId;
    using Harness = PubSubTest::IntegrationHarness;
    using Consumer = PubSubTest::Consumer;
    using NodeMask = PubSubTest::NodeMask;
    using TestPool = Totem::PubSubBackend::Pool<PubSubTest::Message, 64>;
    using RouterTransport =
        Totem::PubSubBackend::Transports::LocalSharedBusRouterTransport;
    using RouterDeps = Totem::PubSubBackend::Transports::
        LocalSharedBusRouterTransportDependencies;
    using SharedBusLink = Totem::PubSubBackend::Transports::LocalSharedBusLink;
    using EdgeTransport =
        Totem::PubSubBackend::Transports::LocalDMABufferedTransport;
    using ActiveEdgeTransport =
        Totem::PubSubBackend::Transports::LocalActiveBufferedTransport;
    using EdgeDeps = Totem::PubSubBackend::Transports::
        LocalSharedBusEdgeTransportDependencies;
    using LocalTransport = Totem::PubSubBackend::Transports::LocalTransport;
    using LocalDeps =
        Totem::PubSubBackend::Transports::LocalTransportDependencies;
    using BaseTransportDeps =
        Totem::PubSubBackend::Transports::BaseTransportDependencies;

    explicit PubSubTestSetup(Totem::TaskController::IRegistry &taskRegistry)
        : pubSubMaster(taskRegistry, NodeId::Master),
          pubSubNodeA1(taskRegistry, NodeId::GPUNode0),
          pubSubNodeA2(taskRegistry, NodeId::GPUNode1),
          pubSubNodeA3(taskRegistry, NodeId::GPUNode2),
          pubSubNodeA4(taskRegistry, NodeId::GPUNode3),
          pubSubBridgeC(taskRegistry, NodeId::Media),
          pubSubNodeD(taskRegistry, NodeId::InputOutput),
          spiRouter(makeRouterDeps(pubSubMaster)),
          spiA1(makeEdgeDeps(pubSubNodeA1, "SPI-A1", NodeId::GPUNode0,
                             spiA1ReadyAfterMs)),
          spiA2(makeEdgeDeps(pubSubNodeA2, "SPI-A2", NodeId::GPUNode1,
                             spiA2ReadyAfterMs)),
          spiA3(makeEdgeDeps(pubSubNodeA3, "SPI-A3", NodeId::GPUNode2,
                             spiA3ReadyAfterMs)),
          spiA4(makeEdgeDeps(pubSubNodeA4, "SPI-A4", NodeId::GPUNode3,
                             spiA4ReadyAfterMs)),
          spiBridgeC(makeEdgeDeps(pubSubBridgeC, "SPI-C", NodeId::Media,
                                  spiBridgeCReadyAfterMs)),
          rs485BridgeC(makeLocalDeps(pubSubBridgeC, "RS485-C", Transport::RS485,
                                     rs485BridgeCReadyAfterMs)),
          rs485NodeD(makeLocalDeps(pubSubNodeD, "RS485-D", Transport::RS485,
                                   rs485NodeDReadyAfterMs)),
          a1MessagePool(PubSubService::nextMessageId),
          cMessagePool(PubSubService::nextMessageId),
          consumerMasterSensor("MasterSensor", NodeId::Master, pubSubHarness),
          consumerA1Sensor("A1Sensor", NodeId::GPUNode0, pubSubHarness),
          consumerA2Sensor("A2Sensor", NodeId::GPUNode1, pubSubHarness),
          consumerA3Sensor("A3Sensor", NodeId::GPUNode2, pubSubHarness),
          consumerA4Sensor("A4Sensor", NodeId::GPUNode3, pubSubHarness),
          consumerDHeartbeat("DHeartbeat", NodeId::InputOutput, pubSubHarness),
          a1PoolProbe(Harness::makePoolProbe("A1 pool", a1MessagePool)),
          cPoolProbe(Harness::makePoolProbe("C pool", cMessagePool)),
          sensorTransportProbes{{
              Harness::makeTransportProbe("SPI-C edge egress", spiBridgeC),
              Harness::makeTransportProbe("SPI router egress", spiRouter),
          }},
          heartbeatTransportProbes{{
              Harness::makeTransportProbe("SPI-A1 edge egress", spiA1),
              Harness::makeTransportProbe("SPI router egress", spiRouter),
              Harness::makeTransportProbe("SPI-C edge egress", spiBridgeC),
              Harness::makeTransportProbe("RS485-C egress", rs485BridgeC),
          }} {}

    void setup() {
        ABORT_IF_ERR_BEGIN(
            pubSubMaster.begin(makePubSubConfig("PubSubMaster", 10, 5)));
        ABORT_IF_ERR_BEGIN(pubSubNodeA1.begin(makePubSubConfig("PubSubA1")));
        ABORT_IF_ERR_BEGIN(pubSubNodeA2.begin(makePubSubConfig("PubSubA2")));
        ABORT_IF_ERR_BEGIN(pubSubNodeA3.begin(makePubSubConfig("PubSubA3")));
        ABORT_IF_ERR_BEGIN(pubSubNodeA4.begin(makePubSubConfig("PubSubA4")));
        ABORT_IF_ERR_BEGIN(
            pubSubBridgeC.begin(makePubSubConfig("PubSubBridgeC")));
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
        ABORT_IF_UNEXPECTED(
            a1SensorSub,
            pubSubNodeA1.subscribe("a1sens",
                                   {.subscriber = &consumerA1Sensor,
                                    .callback = Consumer::callback},
                                   Topic::Sensor),
            "Failed to subscribe A1 to Sensor");
        ABORT_IF_UNEXPECTED(
            a2SensorSub,
            pubSubNodeA2.subscribe("a2sens",
                                   {.subscriber = &consumerA2Sensor,
                                    .callback = Consumer::callback},
                                   Topic::Sensor),
            "Failed to subscribe A2 to Sensor");
        ABORT_IF_UNEXPECTED(
            a3SensorSub,
            pubSubNodeA3.subscribe("a3sens",
                                   {.subscriber = &consumerA3Sensor,
                                    .callback = Consumer::callback},
                                   Topic::Sensor),
            "Failed to subscribe A3 to Sensor");
        ABORT_IF_UNEXPECTED(
            a4SensorSub,
            pubSubNodeA4.subscribe("a4sens",
                                   {.subscriber = &consumerA4Sensor,
                                    .callback = Consumer::callback},
                                   Topic::Sensor),
            "Failed to subscribe A4 to Sensor");
        ABORT_IF_UNEXPECTED(
            dHeartbeatSub,
            pubSubNodeD.subscribe("dbeat",
                                  {.subscriber = &consumerDHeartbeat,
                                   .callback = Consumer::callback},
                                  Topic::Heartbeat),
            "Failed to subscribe node D to Heartbeat");

        (void)masterSensorSub;
        (void)a1SensorSub;
        (void)a2SensorSub;
        (void)a3SensorSub;
        (void)a4SensorSub;
        (void)dHeartbeatSub;

        publishStartMs = ::platform::get_time() + harnessWarmupMs;
        _log_i("PubSubTest: warming up subscriptions for %u ms before "
               "publishing",
               static_cast<unsigned>(harnessWarmupMs));
    }

    [[nodiscard]] static Totem::PubSubBackend::Config
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

    ReturnCode publishTestMessage(
        PubSubNode &node, TestPool &pool, Topic topic, const char *label,
        NodeMask recipients, const Harness::PoolProbe &poolProbe,
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
            .getPayloadPtr = TestPool::getPtr,
            .encodePayload = TestPool::encodePayload,
            .release = TestPool::release,
        };
        ABORT_IF_UNEXPECTED(
            envelope,
            Totem::PubSubBackend::Envelope::make<PubSubTest::Message>(
                envelopeDef),
            "Failed to create test envelope for topic " SV_FMT,
            SV_ARG(magic_enum::enum_name(topic)));

        auto nowMs = ::platform::get_time();
        auto expectationResult =
            pubSubHarness.expect(envelope.header, message, recipients,
                                 poolProbe, transportProbes, nowMs);
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

    ReturnCode work(uint32_t nowMs) {
        pubSubHarness.poll(nowMs, expectationTimeoutMs, harnessReportIntervalMs,
                           targetPropagationLatencyMs);

        if (nowMs < publishStartMs) {
            return OK();
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
        return OK();
    }

  private:
    [[nodiscard]] static RouterDeps makeRouterDeps(PubSubNode &node) {
        return RouterDeps{
            .pubSubNode = static_cast<void *>(&node),
            .transportId = static_cast<uint8_t>(Transport::SPI),
            .name = "SPI-Router",
            .sendAckCallback = PubSubNode::ack,
            .availabilityObserver = &node,
            .wakeCallback = PubSubNode::wake,
            .ingressDispatchCallback = PubSubNode::dispatchIngressFrame,
            .ingress = &node.ingress(),
        };
    }

    [[nodiscard]] static BaseTransportDeps
    makeBaseDeps(PubSubNode &node, const char *name, Transport transport,
                 bool directRelay = true) {
        return BaseTransportDeps{
            .pubSubNode = static_cast<void *>(&node),
            .transportId = static_cast<uint8_t>(transport),
            .name = name,
            .sendAckCallback = PubSubNode::ack,
            .availabilityObserver = &node,
            .wakeCallback = PubSubNode::wake,
            .ingressDispatchCallback =
                directRelay ? PubSubNode::dispatchIngressFrame : nullptr,
            .ingress = node.ingress(),
        };
    }

    [[nodiscard]] static EdgeDeps makeEdgeDeps(PubSubNode &node,
                                               const char *name, NodeId peerId,
                                               uint32_t readyAfterMs,
                                               bool directRelay = true) {
        return EdgeDeps{
            .base = makeBaseDeps(node, name, Transport::SPI, directRelay),
            .peerId = static_cast<PeerId>(peerId),
            .readyAfterMs = readyAfterMs,
        };
    }

    [[nodiscard]] static LocalDeps
    makeLocalDeps(PubSubNode &node, const char *name, Transport transport,
                  uint32_t readyAfterMs, bool directRelay = true) {
        return LocalDeps{
            .base = makeBaseDeps(node, name, transport, directRelay),
            .readyAfterMs = readyAfterMs,
        };
    }

    PubSubNode pubSubMaster;
    PubSubNode pubSubNodeA1;
    PubSubNode pubSubNodeA2;
    PubSubNode pubSubNodeA3;
    PubSubNode pubSubNodeA4;
    PubSubNode pubSubBridgeC;
    PubSubNode pubSubNodeD;

    RouterTransport spiRouter;

    SharedBusLink spiLinkA1{"SPI-Link-A1"};
    SharedBusLink spiLinkA2{"SPI-Link-A2"};
    SharedBusLink spiLinkA3{"SPI-Link-A3"};
    SharedBusLink spiLinkA4{"SPI-Link-A4"};
    SharedBusLink spiLinkBridgeC{"SPI-Link-BridgeC"};

    EdgeTransport spiA1;
    EdgeTransport spiA2;
    EdgeTransport spiA3;
    ActiveEdgeTransport spiA4;
    EdgeTransport spiBridgeC;
    LocalTransport rs485BridgeC;
    LocalTransport rs485NodeD;

    TestPool a1MessagePool;
    TestPool cMessagePool;

    Harness pubSubHarness;
    Consumer consumerMasterSensor;
    Consumer consumerA1Sensor;
    Consumer consumerA2Sensor;
    Consumer consumerA3Sensor;
    Consumer consumerA4Sensor;
    Consumer consumerDHeartbeat;

    Harness::PoolProbe a1PoolProbe;
    Harness::PoolProbe cPoolProbe;
    std::array<Harness::TransportProbe, 2> sensorTransportProbes;
    std::array<Harness::TransportProbe, 4> heartbeatTransportProbes;

    uint32_t publishStartMs = 0;
    uint32_t publishCycle = 0;
};
