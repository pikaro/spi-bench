#pragma once

#include "Data/PubSub.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "TestMessage.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>

namespace PubSubTest {

using Topic = Totem::Data::PubSub::Topic;
using NodeId = Totem::Data::PubSub::NodeId;
using MessageId = Totem::PubSubBackend::detail::MessageId;
using Header = Totem::PubSubBackend::Header;

using NodeMask = uint32_t;

[[nodiscard]] inline constexpr NodeMask nodeMask(NodeId nodeId) {
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
    Message msg{};
    msg.flag = std::rand() % 2 == 0;
    msg.intVal = std::rand() % 1000;
    msg.uint32Val = std::rand() % 100000;
    msg.uint16Val = std::rand() % 65536;
    msg.uint8Val = std::rand() % 256;
    std::strncpy(msg.strVal.data(), "Hello, PubSub!", msg.strVal.size() - 1);
    msg.strVal[msg.strVal.size() - 1] = '\0';
    for (size_t i = 0; i < msg.byteArrayVal.size(); ++i) {
        msg.byteArrayVal[i] = std::byte(std::rand() % 256);
    }
    return msg;
}

class IntegrationHarness {
  public:
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
    [[nodiscard]] static PoolProbe makePoolProbe(const char *name,
                                                 const Pool &pool) {
        return PoolProbe{
            .name = name,
            .ctx = &pool,
            .wasFreed = [](const void *ctx, MessageId messageId) -> bool {
                auto *typedPool = static_cast<const Pool *>(ctx);
                return typedPool->wasFreed(messageId);
            },
        };
    }

    template <typename Transport>
    [[nodiscard]] static TransportProbe
    makeTransportProbe(const char *name, const Transport &transport) {
        return TransportProbe{
            .name = name,
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
        if (auto *expectation = _findExpectation(header)) {
            expectation->occupied = false;
        }
        ++_stats.publishFailures;
    }

    ReturnCode recordPublicationAttempt() {
        ++_stats.publishAttempts;
        return OK();
    }

    ReturnCode recordReceipt(NodeId recipient,
                             const Totem::PubSubBackend::Envelope &envelope) {
        FAIL_IF_UNEXPECTED_FWD(message, envelope.getPayloadAs<Message>(),
                               "Failed to decode message payload");

        auto *expectation = _findExpectation(envelope.header);
        if (expectation == nullptr) {
            ++_stats.unexpectedMessages;
            _log_e("PubSubTest: unexpected message for source %u messageId %u "
                   "topic " SV_FMT,
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

    void poll(uint32_t nowMs, uint32_t timeoutMs, uint32_t reportIntervalMs,
              uint32_t targetLatencyMs = 0) {
        _evaluateExpectations(nowMs, timeoutMs, targetLatencyMs);
        if (reportIntervalMs == 0) {
            return;
        }
        if ((nowMs - _lastReportAtMs) < reportIntervalMs) {
            return;
        }
        _lastReportAtMs = nowMs;
        _log_i("PubSubTest: attempts=%" PRIu32 " pending=%zu complete=%" PRIu32
               " maxReceiptMs=%" PRIu32 " maxCompleteMs=%" PRIu32
               " targetMiss=%" PRIu32 " publishFail=%" PRIu32
               " unexpected=%" PRIu32 " badRecipient=%" PRIu32
               " duplicate=%" PRIu32 " mismatch=%" PRIu32
               " receiptTimeout=%" PRIu32 " poolTimeout=%" PRIu32
               " egressTimeout=%" PRIu32,
               _stats.publishAttempts, pendingCount(), _stats.completions,
               _stats.maxReceiptLatencyMs, _stats.maxCompletionLatencyMs,
               _stats.latencyTargetMisses, _stats.publishFailures,
               _stats.unexpectedMessages, _stats.unexpectedRecipients,
               _stats.duplicateReceipts, _stats.payloadMismatches,
               _stats.receiptTimeouts, _stats.poolFreeTimeouts,
               _stats.egressFreeTimeouts);
    }

    [[nodiscard]] size_t pendingCount() const {
        size_t pending = 0;
        for (const auto &expectation : _expectations) {
            if (expectation.occupied) {
                ++pending;
            }
        }
        return pending;
    }

    [[nodiscard]] const Stats &stats() const { return _stats; }

  private:
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

    void _evaluateExpectations(uint32_t nowMs, uint32_t timeoutMs,
                               uint32_t targetLatencyMs) {
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

            if (!receivedAll) {
                ++_stats.receiptTimeouts;
                _log_e("PubSubTest: receipt timeout for source %u messageId %u "
                       "topic " SV_FMT " missing recipients mask 0x%08" PRIx32,
                       static_cast<unsigned>(expectation.header.source),
                       expectation.header.messageId,
                       MAGIC_SV_ARG(Totem::Data::PubSub::Topic,
                                    expectation.header.topic),
                       _missingRecipients(expectation));
            }

            if (!poolFreed) {
                ++_stats.poolFreeTimeouts;
                _log_e("PubSubTest: pool release timeout for %s source %u "
                       "messageId %u topic " SV_FMT,
                       expectation.poolProbe.name,
                       static_cast<unsigned>(expectation.header.source),
                       expectation.header.messageId,
                       MAGIC_SV_ARG(Totem::Data::PubSub::Topic,
                                    expectation.header.topic));
            }

            if (!egressFreed) {
                for (size_t i = 0; i < expectation.transportProbeCount; ++i) {
                    const auto &probe = expectation.transportProbes[i];
                    if (!probe.wasFreed(probe.ctx, expectation.header)) {
                        ++_stats.egressFreeTimeouts;
                        _log_e("PubSubTest: egress release timeout for %s "
                               "source %u messageId %u topic " SV_FMT,
                               probe.name,
                               static_cast<unsigned>(expectation.header.source),
                               expectation.header.messageId,
                               MAGIC_SV_ARG(Totem::Data::PubSub::Topic,
                                            expectation.header.topic));
                    }
                }
            }

            expectation.occupied = false;
        }
    }

    std::array<Expectation, maxExpectations> _expectations{};
    Stats _stats{};
    uint32_t _lastReportAtMs = 0;
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
