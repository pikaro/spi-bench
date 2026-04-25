#pragma once

#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/Interfaces/Wire.hpp"
#include "PubSubBackend/Transports/BaseTransport.hpp"
#include "PubSubBackend/Transports/LocalSharedBusEdgeTransport.hpp"
#include "PubSubBackend/detail/EgressBuffer.hpp"
#include "PubSubBackend/detail/ITransport.hpp"
#include "PubSubBackend/detail/IngressBuffer.hpp"
#include "PubSubBackend/detail/Metrics.hpp"
#include "PubSubBackend/detail/SerDe.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Queue/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <expected>
#include <limits>
#include <span>
#include <string_view>

namespace Totem::PubSubBackend::Transports {

struct LocalSharedBusRouterTransportDependencies {
    void *pubSubNode = nullptr;
    detail::TransportId transportId = 0;
    std::string_view name;
    SendAckCallback sendAckCallback = nullptr;
    detail::ITransportAvailabilityObserver *availabilityObserver = nullptr;
    WakeCallback wakeCallback = nullptr;
    detail::IngressDispatchCallback ingressDispatchCallback = nullptr;
    detail::IngressBuffer *ingress = nullptr;

    [[nodiscard]] bool valid() const {
        return pubSubNode != nullptr && transportId != 0 && !name.empty() &&
               sendAckCallback != nullptr && ingress != nullptr;
    }
};

struct LocalSharedBusRouterEgressByteArenaConfig {
    static constexpr size_t bufferSize = 1024;
    static constexpr size_t slotCount = 32;
    static constexpr size_t spanCount = 64;
    static constexpr size_t maxRecordAgeMs = 1000;

    [[nodiscard]] static bool isCritical(const Header &header) {
        return header.trafficClass == TrafficClass::Critical;
    }

    static ReturnCode onEvictNoncritical(const Header & /*unused*/) {
        return detail::metrics().addEgressEvictedNoncritical();
    }

    static ReturnCode onDropNoncritical(const Header & /*unused*/) {
        return detail::metrics().addEgressDroppedNoncritical();
    }

    static ReturnCode onRejectCritical(const Header & /*unused*/) {
        return detail::metrics().addEgressRejectedCritical();
    }
};

/**
 * Local test transport that models a shared-bus router.
 *
 * The router polls edge transports for ingress data, stores fanout frames in
 * transport-owned egress memory, and redistributes them to selected peers over
 * subsequent send turns.
 */
class LocalSharedBusRouterTransport
    : public HasLifecycle<LocalSharedBusRouterTransport>,
      public detail::ITransport {
    friend class HasLifecycle<LocalSharedBusRouterTransport>;
    friend struct LifecycleContract<LocalSharedBusRouterTransport>;

    struct PendingDispatch {
        Header header{};
        detail::PeerMask remainingPeers = 0;
    };

  public:
    explicit LocalSharedBusRouterTransport(
        const LocalSharedBusRouterTransportDependencies &deps)
        : _pubSubNode(deps.pubSubNode), _transportId(deps.transportId),
          _instanceName(deps.name), _sendAckCallback(deps.sendAckCallback),
          _availabilityObserver(deps.availabilityObserver),
          _wakeCallback(deps.wakeCallback),
          _ingressDispatchCallback(deps.ingressDispatchCallback),
          _ingress(*deps.ingress) {
        ABORT_IF_NOT(deps.valid(),
                     "Invalid LocalSharedBusRouterTransport dependencies");
    }

    DELETE_COPY(LocalSharedBusRouterTransport)
    DELETE_MOVE(LocalSharedBusRouterTransport)

    static constexpr const char *name = "LocalSharedBusRouterTransport";

    [[nodiscard]] detail::TransportId transportId() const {
        return _transportId;
    }
    [[nodiscard]] std::string_view instanceName() const {
        return _instanceName;
    }
    [[nodiscard]] detail::TransportForwardingPolicy forwardingPolicy() const {
        return detail::TransportForwardingPolicy::SharedBusRouter;
    }
    [[nodiscard]] detail::PeerMask knownPeers() const {
        return _availablePeers();
    }
    [[nodiscard]] bool available() const { return true; }

    /**
     * Report whether router fanout still retains the serialized frame in
     * transport-owned egress memory.
     */
    [[nodiscard]] bool hasBufferedFrame(const Header &header) const {
        return _egressBuffer.contains(header);
    }

    [[nodiscard]] bool wasFrameFreed(const Header &header) const {
        return _egressBuffer.wasFreed(header);
    }

    ReturnCode addPeer(LocalSharedBusEdgeTransport &peer) {
        FAIL_IF_INACTIVE_ERR(
            "Cannot attach peer to inactive shared-bus router");
        FAIL_IF_NOT(peer.active(), ERR(InvalidState),
                    "Cannot attach inactive shared-bus peer");
        FAIL_IF_UNEXPECTED_FWD(peerIndex, detail::peerIndex(peer.peerId()),
                               "Invalid shared-bus peer ID");
        FAIL_IF(_peers[peerIndex] != nullptr, ERR(AlreadyExists),
                "Shared-bus peer %u already attached", peer.peerId());
        _peers[peerIndex] = &peer;
        _attachedPeers |= peer.peerId();
        peer.setRouterWake(_pubSubNode, _wakeCallback);
        _log_i("%s: attached shared-bus peer %u as " SV_FMT, name,
               peer.peerId(), SV_ARG(peer.instanceName()));
        return OK();
    }

    ReturnCode enqueue(detail::FrameHandle frameHandle,
                       const detail::TransportDispatch &dispatch = {}) {
        FAIL_IF_INACTIVE_ERR("Cannot enqueue inactive shared-bus router");
        FAIL_IF(!dispatch.hasTargetPeers(), ERR(InvalidArgument),
                "Shared-bus router enqueue requires target peers");
        // Keep router scratch frames on the stack. They are small, moving them
        // into shared member storage did not improve measured stack use, and
        // stack-local scratch avoids coupling future transport reentrancy to
        // shared mutable state.
        auto enqueueBuffer = std::array<std::byte, bufferSize>{};
        const auto frameSize =
            detail::SerDe::encodedSize(frameHandle->envelope.header);
        if (frameSize > enqueueBuffer.size()) {
            return ERR(Overflow);
        }
        auto frame =
            std::span<const std::byte>{enqueueBuffer.data(), frameSize};
        if (frameHandle->envelope.owner == &_ingress &&
            _ingress.hasSerializedFrame(frameHandle->envelope.header)) {
            FAIL_IF_ERR_FWD(
                _ingress.getSerializedFrame(
                    frameHandle->envelope.header,
                    std::span<std::byte>{enqueueBuffer.data(), frameSize}),
                "Failed to reuse serialized ingress frame for shared-bus "
                "fanout");
        } else {
            FAIL_IF_UNEXPECTED_FWD(
                encodedSize,
                detail::SerDe::serialize(frameHandle->envelope, enqueueBuffer),
                "Failed to serialize shared-bus router frame");
            (void)encodedSize;
        }
        auto pending = PendingDispatch{
            .header = frameHandle->envelope.header,
            .remainingPeers = static_cast<detail::PeerMask>(
                dispatch.targetPeers & _attachedPeers),
        };
        FAIL_IF(pending.remainingPeers == 0, ERR(InvalidArgument),
                "Shared-bus router fanout has no attached target peers");
        FAIL_IF_ERR_FWD(
            _sendPending(pending, frame),
            "Failed to fan out shared-bus router frame");
        FAIL_IF_ERR_FWD(_ack(frameHandle->envelope),
                        "Failed to acknowledge shared-bus router ownership");
        return OK();
    }

    ReturnCode enqueueRaw(const Header &header, std::span<const std::byte> frame,
                          const detail::TransportDispatch &dispatch = {})
        override {
        FAIL_IF_INACTIVE_ERR("Cannot enqueue inactive shared-bus router");
        FAIL_IF(!dispatch.hasTargetPeers(), ERR(InvalidArgument),
                "Shared-bus router raw enqueue requires target peers");
        FAIL_IF_UNEXPECTED_FWD(stored, _egressBuffer.store(header, frame),
                               "Failed to store direct shared-bus router "
                               "frame");
        if (!stored) {
            return ERR(Timeout);
        }
        auto pending = PendingDispatch{
            .header = header,
            .remainingPeers = static_cast<detail::PeerMask>(
                dispatch.targetPeers & _attachedPeers),
        };
        FAIL_IF(pending.remainingPeers == 0, ERR(InvalidArgument),
                "Shared-bus router direct fanout has no attached target "
                "peers");
        auto queueRet =
            Totem::Queue::Platform::send(_sendQueue, &pending, queueSendTimeoutTicks);
        if (!queueRet.ok()) {
            FAIL_IF_ERR_FWD(_egressBuffer.release(header),
                            "Failed to roll back direct shared-bus router "
                            "frame after queue failure");
            FAIL(queueRet,
                 "Failed to queue direct shared-bus router dispatch: " ERR_FMT,
                 ERR_ARG(queueRet));
        }
        FAIL_IF_ERR_FWD(_wake(), "Failed to wake shared-bus router sender");
        return OK();
    }

    ReturnCode send(size_t maxCount = std::numeric_limits<size_t>::max()) {
        FAIL_IF_INACTIVE_ERR("Cannot send with inactive shared-bus router");
        FAIL_IF_ERR_FWD(_observePeerAvailability(),
                        "Failed to observe shared-bus peer availability");
        auto ret = OK();
        size_t count = 0;

        while (ret.ok() && count < maxCount) {
            PendingDispatch pending;
            ret.combine(
                Totem::Queue::Platform::receive(_sendQueue, &pending, 0));
            if (!ret.ok()) {
                break;
            }
            FAIL_IF_ERR_FWD(_sendPending(pending, count, maxCount),
                            "Failed to send shared-bus router dispatch");
        }

        if (!ret.ok()) {
            if (ret == ERR(Timeout)) {
                return OK();
            }
            FAIL(ret, "Failed to dequeue shared-bus router dispatch: " ERR_FMT,
                 ERR_ARG(ret));
        }
        return OK();
    }

    ReturnCode receive(size_t maxCount = std::numeric_limits<size_t>::max()) {
        (void)maxCount;
        FAIL_IF_INACTIVE_ERR("Cannot receive with inactive shared-bus router");
        FAIL_IF_ERR_FWD(_observePeerAvailability(),
                        "Failed to observe shared-bus peer availability");
        return OK();
    }

    ReturnCode pollInto(void *ctx, detail::PollIntoCallback callback,
                        size_t maxCount = std::numeric_limits<size_t>::max()) {
        FAIL_IF_INACTIVE_ERR("Cannot poll inactive shared-bus router");
        FAIL_IF_ERR_FWD(_observePeerAvailability(),
                        "Failed to observe shared-bus peer availability");
        if (_attachedPeers == 0) {
            return OK();
        }

        size_t count = 0;
        while (count < maxCount) {
            bool madeProgress = false;
            for (size_t scanned = 0;
                 scanned < detail::maxPeerCount && count < maxCount;
                 ++scanned) {
                const auto index = static_cast<size_t>(
                    (_receiveCursor + scanned) % detail::maxPeerCount);
                auto *peer = _peers[index];
                if (peer == nullptr) {
                    continue;
                }
                auto receiveBuffer = std::array<std::byte, bufferSize>{};
                auto receiveResult = peer->takeFrameForRouter(receiveBuffer);
                if (!receiveResult) {
                    if (receiveResult.error() == ERR(Timeout)) {
                        continue;
                    }
                    FAIL(receiveResult.error(),
                         "Failed to poll shared-bus peer %u: " ERR_FMT,
                         peer->peerId(), ERR_ARG(receiveResult.error()));
                }

                auto handled = false;
                if (_ingressDispatchCallback != nullptr) {
                    FAIL_IF_UNEXPECTED_FWD(
                        dispatched,
                        _ingressDispatchCallback(
                            _pubSubNode,
                            std::span<const std::byte>{
                                receiveBuffer.data(), *receiveResult},
                            detail::IngressContext{
                                .transportId = _transportId,
                                .peerId = peer->peerId(),
                            }),
                        "Failed to dispatch shared-bus router ingress frame");
                    handled = dispatched;
                }
                if (handled) {
                    ++count;
                    madeProgress = true;
                    _receiveCursor = (index + 1) % detail::maxPeerCount;
                    continue;
                }
                FAIL_IF_UNEXPECTED_FWD(
                    envelope,
                    _ingress.storeFrame(std::span<const std::byte>{
                        receiveBuffer.data(), *receiveResult}),
                    "Failed to store shared-bus router ingress frame");
                if (!envelope.has_value()) {
                    ++count;
                    madeProgress = true;
                    _receiveCursor = (index + 1) % detail::maxPeerCount;
                    continue;
                }
                FAIL_IF_ERR_FWD(
                    callback(ctx, *envelope,
                             detail::IngressContext{
                                 .transportId = _transportId,
                                 .peerId = peer->peerId(),
                             }),
                    "Failed to process shared-bus router ingress frame");
                ++count;
                madeProgress = true;
                _receiveCursor = (index + 1) % detail::maxPeerCount;
            }
            if (!madeProgress) {
                return OK();
            }
        }
        return OK();
    }

  private:
    static constexpr size_t bufferSize = detail::SerDe::headerSize +
                                         detail::Spec::Limits::maxPayloadSize +
                                         detail::SerDe::overheadSize;

    ReturnCode _ack(const Envelope &envelope) {
        return _sendAckCallback(_pubSubNode, _transportId, envelope);
    }

    ReturnCode _wake(Signal signal = Signal::Ping) {
        if (_wakeCallback == nullptr) {
            return OK();
        }
        return _wakeCallback(_pubSubNode, signal);
    }

    ReturnCode _observePeerAvailability() {
        const auto availablePeers = _availablePeers();
        const auto newlyAvailablePeers =
            static_cast<detail::PeerMask>(availablePeers & ~_knownAvailablePeers);
        _knownAvailablePeers = availablePeers;
        if (newlyAvailablePeers == 0 || _availabilityObserver == nullptr) {
            return OK();
        }
        FAIL_IF_ERR_FWD(
            _availabilityObserver->onTransportAvailabilityChanged(_transportId,
                                                                  true),
            "Failed to notify shared-bus peer availability");
        for (auto *peer : _peers) {
            if (peer == nullptr) {
                continue;
            }
            FAIL_IF_ERR_FWD(peer->notifySharedBusMembershipChanged(),
                            "Failed to notify shared-bus peer membership "
                            "change");
        }
        return _wake();
    }

    ReturnCode _requeuePending(const PendingDispatch &pending) {
        auto ret = Totem::Queue::Platform::send(_sendQueue, &pending,
                                                queueSendTimeoutTicks);
        if (ret.ok()) {
            return OK();
        }
        _log_w("%s: dropping shared-bus router frame with message ID %lu "
               "because dispatch retry queue is full",
               name, static_cast<unsigned long>(pending.header.messageId));
        return _egressBuffer.release(pending.header);
    }

    ReturnCode _sendPending(PendingDispatch pending,
                            std::span<const std::byte> frame) {
        while (pending.remainingPeers != 0) {
            FAIL_IF_UNEXPECTED_FWD(peerIndex,
                                   _findNextPeerIndex(pending.remainingPeers),
                                   "Failed to find next shared-bus target"
                                   " peer");
            auto *peer = _peers[peerIndex];
            auto deliverRet = peer->receiveFromRouter(frame);
            if (!deliverRet.ok()) {
                if (deliverRet == ERR(Timeout) &&
                    pending.header.trafficClass == TrafficClass::Noncritical) {
                    _log_w("%s: dropped noncritical shared-bus router frame "
                           "with message ID %lu under peer queue pressure",
                           name,
                           static_cast<unsigned long>(pending.header.messageId));
                    return OK();
                }
                FAIL(deliverRet,
                     "Failed to deliver shared-bus frame to peer %u: " ERR_FMT,
                     peer->peerId(), ERR_ARG(deliverRet));
            }
            pending.remainingPeers &= static_cast<detail::PeerMask>(
                ~(static_cast<detail::PeerMask>(1U << peerIndex)));
            _sendCursor = (peerIndex + 1) % detail::maxPeerCount;
        }
        return OK();
    }

    [[nodiscard]] std::expected<size_t, ReturnCode>
    _findNextPeerIndex(detail::PeerMask remainingPeers) {
        for (size_t scanned = 0; scanned < detail::maxPeerCount; ++scanned) {
            const auto index = static_cast<size_t>((_sendCursor + scanned) %
                                                   detail::maxPeerCount);
            const auto peerBit = static_cast<detail::PeerMask>(1U << index);
            if ((remainingPeers & peerBit) == 0) {
                continue;
            }
            FAIL_IF(_peers[index] == nullptr, std::unexpected(ERR(NotFound)),
                    "Shared-bus router missing attached peer for bit %u",
                    static_cast<unsigned>(peerBit));
            return index;
        }
        return std::unexpected(ERR(NotFound));
    }

    [[nodiscard]] detail::PeerMask _availablePeers() const {
        auto availablePeers = static_cast<detail::PeerMask>(0);
        for (size_t i = 0; i < detail::maxPeerCount; ++i) {
            auto *peer = _peers[i];
            if (peer == nullptr || !peer->available()) {
                continue;
            }
            availablePeers |= static_cast<detail::PeerMask>(1U << i);
        }
        return availablePeers;
    }

    ReturnCode _sendPending(PendingDispatch pending, size_t &count,
                            size_t maxCount) {
        auto frameSize = detail::SerDe::encodedSize(pending.header);
        auto sendBuffer = std::array<std::byte, bufferSize>{};
        auto frameSpan = std::span<std::byte>{sendBuffer.data(), frameSize};

        FAIL_IF_ERR_FWD(
            _egressBuffer.getRaw(pending.header, frameSpan),
            "Failed to read shared-bus router frame from egress buffer");

        while (pending.remainingPeers != 0 && count < maxCount) {
            FAIL_IF_UNEXPECTED_FWD(peerIndex,
                                   _findNextPeerIndex(pending.remainingPeers),
                                   "Failed to find next shared-bus target"
                                   " peer");
            auto *peer = _peers[peerIndex];
            auto deliverRet = peer->receiveFromRouter(
                std::span<const std::byte>{sendBuffer.data(), frameSize});
            if (!deliverRet.ok()) {
                FAIL_IF_ERR_FWD(_requeuePending(pending),
                                "Failed to requeue shared-bus router dispatch"
                                " after delivery failure");
                if (deliverRet == ERR(Timeout)) {
                    return OK();
                }
                FAIL(deliverRet,
                     "Failed to deliver shared-bus frame to peer %u: " ERR_FMT,
                     peer->peerId(), ERR_ARG(deliverRet));
            }
            pending.remainingPeers &= static_cast<detail::PeerMask>(
                ~(static_cast<detail::PeerMask>(1U << peerIndex)));
            _sendCursor = (peerIndex + 1) % detail::maxPeerCount;
            ++count;
        }

        if (pending.remainingPeers != 0) {
            FAIL_IF_ERR_FWD(_requeuePending(pending),
                            "Failed to requeue shared-bus router dispatch");
            return OK();
        }

        FAIL_IF_ERR_FWD(_egressBuffer.release(pending.header),
                        "Failed to release shared-bus router frame");
        return OK();
    }

    ReturnCode _onBegin() {
        auto sendQueueResult =
            Totem::Queue::Platform::create(_sendQueueStorage);
        FAIL_IF_ASSIGN_UNEXPECTED_FWD(
            _sendQueue, sendQueueResult,
            "Failed to create shared-bus router send queue: " ERR_FMT,
            ERR_ARG(sendQueueResult.error()));
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        if (_sendQueue != nullptr) {
            ret.combine(Totem::Queue::Platform::destroy(_sendQueue));
            _sendQueue = nullptr;
        }
        return ret;
    }

    void *_pubSubNode = nullptr;
    detail::TransportId _transportId = 0;
    std::string_view _instanceName;
    SendAckCallback _sendAckCallback = nullptr;
    detail::ITransportAvailabilityObserver *_availabilityObserver = nullptr;
    WakeCallback _wakeCallback = nullptr;
    detail::IngressDispatchCallback _ingressDispatchCallback = nullptr;
    detail::IngressBuffer &_ingress;

    detail::EgressBuffer<LocalSharedBusRouterEgressByteArenaConfig>
        _egressBuffer;

    Totem::Queue::Handle _sendQueue{};
    Totem::Queue::Platform::Storage<PendingDispatch,
                                    detail::Spec::Limits::maxMessageQueueSize>
        _sendQueueStorage{};

    std::array<LocalSharedBusEdgeTransport *, detail::maxPeerCount> _peers{};
    detail::PeerMask _attachedPeers = 0;
    detail::PeerMask _knownAvailablePeers = 0;
    size_t _receiveCursor = 0;
    size_t _sendCursor = 0;

    static constexpr ::platform::Tick queueSendTimeoutTicks = 1;
};

inline constexpr LifecycleContract<LocalSharedBusRouterTransport>
    _local_shared_bus_router_transport_lifecycle_contract;

} // namespace Totem::PubSubBackend::Transports
