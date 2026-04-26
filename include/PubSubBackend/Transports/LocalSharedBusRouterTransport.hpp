#pragma once

#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/Interfaces/Wire.hpp"
#include "PubSubBackend/Transports/BaseTransport.hpp"
#include "PubSubBackend/Transports/LocalSharedBusEdgeTransport.hpp"
#include "PubSubBackend/detail/ITransport.hpp"
#include "PubSubBackend/detail/IngressBuffer.hpp"
#include "PubSubBackend/detail/SerDe.hpp"
#include "PubSubBackend/detail/Types.hpp"
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

/**
 * Local test transport that models a shared-bus router.
 *
 * The router polls edge transports for ingress data and synchronously fans
 * frames out to selected peer link queues. Those per-peer queues are the local
 * simulator's durable handoff point.
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
     * Shared-bus router simulation hands durable ownership to peer link queues
     * and does not retain separate egress records.
     */
    [[nodiscard]] bool hasBufferedFrame(const Header &header) const {
        (void)header;
        return false;
    }

    [[nodiscard]] bool wasFrameFreed(const Header &header) const {
        (void)header;
        return true;
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
        std::array<std::byte, bufferSize> enqueueBuffer;
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
        auto pending = PendingDispatch{
            .header = header,
            .remainingPeers = static_cast<detail::PeerMask>(
                dispatch.targetPeers & _attachedPeers),
        };
        FAIL_IF(pending.remainingPeers == 0, ERR(InvalidArgument),
                "Shared-bus router direct fanout has no attached target "
                "peers");
        return _sendPending(pending, frame);
    }

    ReturnCode send(size_t maxCount = std::numeric_limits<size_t>::max()) {
        (void)maxCount;
        FAIL_IF_INACTIVE_ERR("Cannot send with inactive shared-bus router");
        FAIL_IF_ERR_FWD(_observePeerAvailability(),
                        "Failed to observe shared-bus peer availability");
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
                std::array<std::byte, bufferSize> receiveBuffer;
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

    ReturnCode _sendPending(PendingDispatch pending,
                            std::span<const std::byte> frame) {
        while (pending.remainingPeers != 0) {
            FAIL_IF_UNEXPECTED_FWD(peerIndex,
                                   _findNextPeerIndex(pending.remainingPeers),
                                   "Failed to find next shared-bus target"
                                   " peer");
            auto *peer = _peers[peerIndex];
            const auto peerBit =
                static_cast<detail::PeerMask>(1U << peerIndex);
            auto deliverRet = peer->receiveFromRouter(frame);
            if (!deliverRet.ok()) {
                if (deliverRet == ERR(Timeout) &&
                    pending.header.trafficClass == TrafficClass::Noncritical) {
                    _log_w("%s: dropped noncritical shared-bus router frame "
                           "with message ID %lu for peer %u under peer queue "
                           "pressure",
                           name,
                           static_cast<unsigned long>(pending.header.messageId),
                           static_cast<unsigned>(peer->peerId()));
                    pending.remainingPeers &=
                        static_cast<detail::PeerMask>(~peerBit);
                    _sendCursor = (peerIndex + 1) % detail::maxPeerCount;
                    continue;
                }
                FAIL(deliverRet,
                     "Failed to deliver shared-bus frame to peer %u: " ERR_FMT,
                     peer->peerId(), ERR_ARG(deliverRet));
            }
            pending.remainingPeers &= static_cast<detail::PeerMask>(~peerBit);
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

    ReturnCode _onBegin() { return OK(); }

    ReturnCode _onEnd() { return OK(); }

    void *_pubSubNode = nullptr;
    detail::TransportId _transportId = 0;
    std::string_view _instanceName;
    SendAckCallback _sendAckCallback = nullptr;
    detail::ITransportAvailabilityObserver *_availabilityObserver = nullptr;
    WakeCallback _wakeCallback = nullptr;
    detail::IngressDispatchCallback _ingressDispatchCallback = nullptr;
    detail::IngressBuffer &_ingress;

    std::array<LocalSharedBusEdgeTransport *, detail::maxPeerCount> _peers{};
    detail::PeerMask _attachedPeers = 0;
    detail::PeerMask _knownAvailablePeers = 0;
    size_t _receiveCursor = 0;
    size_t _sendCursor = 0;
};

inline constexpr LifecycleContract<LocalSharedBusRouterTransport>
    _local_shared_bus_router_transport_lifecycle_contract;

} // namespace Totem::PubSubBackend::Transports
