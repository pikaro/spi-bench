#pragma once

#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "Mutex/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Wire.hpp"
#include "PubSubBackend/Transports/BaseTransport.hpp"
#include "PubSubBackend/detail/ITransport.hpp"
#include "PubSubBackend/detail/Metrics.hpp"
#include "PubSubBackend/detail/SerDe.hpp"
#include "PubSubBackend/detail/Trace.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Queue/Facade.hpp"
#include "Types/Error.hpp"
#include "Wire/Interfaces/Request.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <span>
#include <string_view>

namespace Totem::PubSubBackend::Transports {

template <class Link> struct SpiRouterPeerDependencies {
    PeerId peerId = 0;
    Link *link = nullptr;
    std::string_view name;

    [[nodiscard]] bool valid() const {
        return peerId != 0 && link != nullptr && !name.empty();
    }
};

template <class Link, size_t PeerCount>
struct SpiRouterTransportDependencies {
    void *pubSubNode = nullptr;
    detail::TransportId transportId = 0;
    std::string_view name;
    SendAckCallback sendAckCallback = nullptr;
    detail::ITransportAvailabilityObserver *availabilityObserver = nullptr;
    WakeCallback wakeCallback = nullptr;
    detail::IngressDispatchCallback ingressDispatchCallback = nullptr;
    detail::IngressBuffer *ingress = nullptr;
    std::array<SpiRouterPeerDependencies<Link>, PeerCount> peers{};

    [[nodiscard]] bool valid() const {
        if (pubSubNode == nullptr || transportId == 0 || name.empty() ||
            sendAckCallback == nullptr || ingress == nullptr) {
            return false;
        }
        for (const auto &peer : peers) {
            if (!peer.valid()) {
                return false;
            }
        }
        return true;
    }
};

template <class Link, size_t PeerCount>
class SpiRouterTransport
    : public HasLifecycle<SpiRouterTransport<Link, PeerCount>>,
      public detail::ITransport {
    friend class HasLifecycle<SpiRouterTransport<Link, PeerCount>>;
    friend struct LifecycleContract<SpiRouterTransport<Link, PeerCount>>;

    static constexpr size_t rxQueueDepth = 8;
    static constexpr size_t inFlightDepth = 8;
    static constexpr size_t deferredRawDepth = 8;
    static constexpr size_t pendingDepth = 8;
    static constexpr size_t bufferSize = detail::SerDe::headerSize +
                                         detail::Spec::Limits::maxPayloadSize +
                                         detail::SerDe::overheadSize;

    struct Peer;
    struct PendingFrame;

    struct RxFrame {
        std::array<std::byte, bufferSize> data{};
        size_t size = 0;
    };

    struct RawFrame {
        Header header{};
        std::array<std::byte, bufferSize> data{};
        size_t size = 0;
    };

  public:
    struct Stats {
        uint32_t txQueued = 0;
        uint32_t txAcked = 0;
        uint32_t txFailed = 0;
        uint32_t txInFlightFull = 0;
        uint32_t txSerializeDropped = 0;
        uint32_t rxQueued = 0;
        uint32_t rxDropped = 0;
        uint32_t txTimingSamples = 0;
        uint32_t txQueueWaitMinUs = 0;
        uint32_t txQueueWaitAvgUs = 0;
        uint32_t txQueueWaitMaxUs = 0;
        uint32_t txWireMinUs = 0;
        uint32_t txWireAvgUs = 0;
        uint32_t txWireMaxUs = 0;
        uint32_t txTotalMinUs = 0;
        uint32_t txTotalAvgUs = 0;
        uint32_t txTotalMaxUs = 0;
    };

  private:
    struct InFlightFrame {
        SpiRouterTransport *transport = nullptr;
        Peer *peer = nullptr;
        PendingFrame *pending = nullptr;
        Header header{};
        std::array<std::byte, bufferSize> data{};
        size_t size = 0;
        int64_t queuedAtUs = 0;
        bool occupied = false;
        bool acknowledgeOnComplete = false;
    };

    struct PendingFrame {
        Envelope envelope{};
        uint8_t pendingCount = 0;
        bool occupied = false;
    };

    struct PeerStats {
        std::atomic<uint32_t> txQueued{0};
        std::atomic<uint32_t> txAcked{0};
        std::atomic<uint32_t> txFailed{0};
        std::atomic<uint32_t> txInFlightFull{0};
        std::atomic<uint32_t> txSerializeDropped{0};
        std::atomic<uint32_t> rxQueued{0};
        std::atomic<uint32_t> rxDropped{0};
        std::atomic<uint32_t> txTimingSamples{0};
        std::atomic<uint32_t> txQueueWaitSumUs{0};
        std::atomic<uint32_t> txQueueWaitMinUs{
            std::numeric_limits<uint32_t>::max()};
        std::atomic<uint32_t> txQueueWaitMaxUs{0};
        std::atomic<uint32_t> txWireSumUs{0};
        std::atomic<uint32_t> txWireMinUs{
            std::numeric_limits<uint32_t>::max()};
        std::atomic<uint32_t> txWireMaxUs{0};
        std::atomic<uint32_t> txTotalSumUs{0};
        std::atomic<uint32_t> txTotalMinUs{
            std::numeric_limits<uint32_t>::max()};
        std::atomic<uint32_t> txTotalMaxUs{0};
    };

    struct Peer {
        SpiRouterTransport *transport = nullptr;
        Link *link = nullptr;
        PeerId peerId = 0;
        std::string_view name;
        Totem::Queue::Handle rxFrameQueue{};
        Totem::Queue::Platform::Storage<RxFrame, rxQueueDepth> rxStorage{};
        Totem::Queue::Handle rawSendQueue{};
        Totem::Queue::Platform::Storage<RawFrame, deferredRawDepth>
            rawSendStorage{};
        std::array<InFlightFrame, inFlightDepth> inFlight{};
        PeerStats stats{};
        std::atomic<uint32_t> rawSendQueued{0};
    };

  public:
    explicit SpiRouterTransport(
        const SpiRouterTransportDependencies<Link, PeerCount> &deps)
        : _pubSubNode(deps.pubSubNode), _transportId(deps.transportId),
          _instanceName(deps.name), _sendAckCallback(deps.sendAckCallback),
          _availabilityObserver(deps.availabilityObserver),
          _wakeCallback(deps.wakeCallback),
          _ingressDispatchCallback(deps.ingressDispatchCallback),
          _ingress(*deps.ingress) {
        ABORT_IF_NOT(deps.valid(), "Invalid SpiRouterTransport dependencies");
        for (size_t i = 0; i < PeerCount; ++i) {
            _peers[i].transport = this;
            _peers[i].link = deps.peers[i].link;
            _peers[i].peerId = deps.peers[i].peerId;
            _peers[i].name = deps.peers[i].name;
        }
    }

    DELETE_COPY(SpiRouterTransport)
    DELETE_MOVE(SpiRouterTransport)

    static constexpr const char *name = "SpiRouterTransport";

    ReturnCode registerHandler() {
        auto ret = OK();
        for (auto &peer : _peers) {
            ret.combine(peer.link->registerHandler(Wire::FrameHandler{
                .owner = &peer,
                .payloadType = Wire::PayloadType::PubSub,
                .response = {},
                .onData = _onWireData,
                .onRequest = nullptr,
            }));
        }
        return ret;
    }

    [[nodiscard]] detail::TransportId transportId() const override {
        return _transportId;
    }
    [[nodiscard]] std::string_view instanceName() const override {
        return _instanceName;
    }
    [[nodiscard]] detail::TransportForwardingPolicy
    forwardingPolicy() const override {
        return detail::TransportForwardingPolicy::SharedBusRouter;
    }
    [[nodiscard]] detail::PeerMask knownPeers() const override {
        return _availablePeers();
    }
    [[nodiscard]] bool available() const override {
        return _availablePeers() != 0;
    }

    [[nodiscard]] Stats takeStats(PeerId peerId) {
        auto *peer = _peerById(peerId);
        if (peer == nullptr) {
            return {};
        }
        return _takeStats(peer->stats);
    }

    ReturnCode enqueue(detail::FrameHandle frameHandle,
                       const detail::TransportDispatch &dispatch = {}) override {
        FAIL_IF_INACTIVE_ERR("Cannot enqueue inactive SPI router transport");
        FAIL_IF(!dispatch.hasTargetPeers(), ERR(CoreError, InvalidArgument),
                "SPI router enqueue requires target peers");
        FAIL_IF_ERR_FWD(_observePeerAvailability(),
                        "Failed to observe SPI router peer availability");

        std::array<std::byte, bufferSize> sendBuffer{};
        auto frameSizeResult = _prepareFrameForSend(frameHandle, sendBuffer);
        if (!frameSizeResult) {
            detail::metrics().addSpiDrop();
            _log_w(SV_FMT ": dropping unserializable shared SPI message %u: "
                   ERR_FMT,
                   SV_ARG(_instanceName),
                   frameHandle == nullptr ? 0
                                          : frameHandle->envelope.header.messageId,
                   ERR_ARG(frameSizeResult.error()));
            return frameSizeResult.error();
        }

        auto targetPeers = static_cast<detail::PeerMask>(
            dispatch.targetPeers & _configuredPeerMask());
        const auto targetCount = _countTargetPeers(targetPeers);
        if (targetCount == 0) {
            return _ack(frameHandle->envelope);
        }

        auto *pending = _reservePending(frameHandle->envelope, targetCount);
        FAIL_IF_NULL(pending, ERR(CoreError, Overflow),
                     "No SPI router pending frame slots available");

        const auto frame = std::span<const std::byte>{sendBuffer.data(),
                                                      *frameSizeResult};
        auto ret = OK();
        for (auto &peer : _peers) {
            const auto peerBit = peer.peerId;
            if ((targetPeers & peerBit) == 0) {
                continue;
            }
            auto sendRet = _queuePeerWrite(peer, frameHandle->envelope.header,
                                           frame, pending, true);
            if (!sendRet.ok()) {
                _log_w(SV_FMT ": dropped shared SPI target " SV_FMT
                       " for PubSub message %u: " ERR_FMT,
                       SV_ARG(_instanceName), SV_ARG(peer.name),
                       frameHandle->envelope.header.messageId,
                       ERR_ARG(sendRet));
                Envelope ackEnvelope{};
                if (_releasePendingTarget(*pending, ackEnvelope)) {
                    ret.combine(_ack(ackEnvelope));
                }
                continue;
            }
        }

        return ret;
    }

    ReturnCode enqueueRaw(const Header &header, std::span<const std::byte> frame,
                          const detail::TransportDispatch &dispatch = {})
        override {
        FAIL_IF_INACTIVE_ERR("Cannot enqueue raw inactive SPI router transport");
        FAIL_IF(!dispatch.hasTargetPeers(), ERR(CoreError, InvalidArgument),
                "SPI router raw enqueue requires target peers");
        FAIL_IF(frame.size() > bufferSize, ERR(CoreError, Overflow),
                "Raw shared SPI PubSub frame exceeds transport buffer size");
        FAIL_IF_ERR_FWD(_observePeerAvailability(),
                        "Failed to observe SPI router peer availability");

        auto sentCount = uint8_t{0};
        ReturnCode lastError = ERR(CoreError, Overflow);
        auto targetPeers = static_cast<detail::PeerMask>(
            dispatch.targetPeers & _configuredPeerMask());
        for (auto &peer : _peers) {
            const auto peerBit = peer.peerId;
            if ((targetPeers & peerBit) == 0) {
                continue;
            }
            auto sendRet = _sendRawFrame(peer, header, frame);
            if (!sendRet.ok()) {
                lastError = sendRet;
                _log_w(SV_FMT ": dropped direct shared SPI target " SV_FMT
                       " for PubSub message %u: " ERR_FMT,
                       SV_ARG(_instanceName), SV_ARG(peer.name),
                       header.messageId, ERR_ARG(sendRet));
                continue;
            }
            ++sentCount;
        }
        return sentCount == 0 ? lastError : OK();
    }

    ReturnCode send(size_t maxCount = std::numeric_limits<size_t>::max())
        override {
        FAIL_IF_INACTIVE_ERR("Cannot send with inactive SPI router transport");
        FAIL_IF_ERR_FWD(_observePeerAvailability(),
                        "Failed to observe SPI router peer availability");

        size_t count = 0;
        while (count < maxCount) {
            bool sentRaw = false;
            FAIL_IF_ERR_FWD(_sendDeferredRawFrame(sentRaw),
                            "Failed to send deferred raw shared SPI frame");
            if (!sentRaw) {
                return OK();
            }
            ++count;
        }
        return OK();
    }

    ReturnCode receive(size_t maxCount = std::numeric_limits<size_t>::max())
        override {
        (void)maxCount;
        FAIL_IF_INACTIVE_ERR("Cannot receive with inactive SPI router transport");
        return _observePeerAvailability();
    }

    ReturnCode pollInto(void *ctx, detail::PollIntoCallback callback,
                        size_t maxCount = std::numeric_limits<size_t>::max())
        override {
        FAIL_IF_INACTIVE_ERR("Cannot poll inactive SPI router transport");
        FAIL_IF_ERR_FWD(_observePeerAvailability(),
                        "Failed to observe SPI router peer availability");

        size_t count = 0;
        while (count < maxCount) {
            bool madeProgress = false;
            for (size_t scanned = 0; scanned < PeerCount && count < maxCount;
                 ++scanned) {
                const auto index = (_receiveCursor + scanned) % PeerCount;
                auto &peer = _peers[index];
                RxFrame rxFrame{};
                auto receiveRet = Totem::Queue::Platform::receive(
                    peer.rxFrameQueue, &rxFrame, 0);
                if (!receiveRet.ok()) {
                    if (receiveRet == ERR(CoreError, Timeout) ||
                        receiveRet == ERR(Timeout)) {
                        continue;
                    }
                    FAIL(receiveRet,
                         "Failed to receive shared SPI frame from peer "
                         SV_FMT ": " ERR_FMT,
                         SV_ARG(peer.name), ERR_ARG(receiveRet));
                }

                auto frame = std::span<const std::byte>{rxFrame.data.data(),
                                                        rxFrame.size};
                if (_ingressDispatchCallback != nullptr) {
                    FAIL_IF_UNEXPECTED_FWD(
                        dispatched,
                        _ingressDispatchCallback(
                            _pubSubNode, frame,
                            detail::IngressContext{
                                .transportId = _transportId,
                                .peerId = peer.peerId,
                            }),
                        "Failed to dispatch shared SPI ingress frame");
                    if (dispatched) {
                        ++count;
                        madeProgress = true;
                        _receiveCursor = (index + 1) % PeerCount;
                        continue;
                    }
                }

                FAIL_IF_UNEXPECTED_FWD(
                    envelope, _ingress.storeFrame(frame),
                    "Failed to store shared SPI ingress frame");
                if (!envelope.has_value()) {
                    ++count;
                    madeProgress = true;
                    _receiveCursor = (index + 1) % PeerCount;
                    continue;
                }
                FAIL_IF_ERR_FWD(
                    callback(ctx, *envelope,
                             detail::IngressContext{
                                 .transportId = _transportId,
                                 .peerId = peer.peerId,
                             }),
                    "Failed to process shared SPI ingress frame");
                ++count;
                madeProgress = true;
                _receiveCursor = (index + 1) % PeerCount;
            }
            if (!madeProgress) {
                return OK();
            }
        }
        return OK();
    }

  private:
    [[nodiscard]] static Stats _takeStats(PeerStats &stats) {
        auto take = [](std::atomic<uint32_t> &value) {
            return value.exchange(0, std::memory_order_relaxed);
        };
        auto takeMin = [](std::atomic<uint32_t> &value) {
            const auto min = value.exchange(
                std::numeric_limits<uint32_t>::max(),
                std::memory_order_relaxed);
            return min == std::numeric_limits<uint32_t>::max() ? 0 : min;
        };
        const auto timingSamples = take(stats.txTimingSamples);
        const auto queueWaitSum = take(stats.txQueueWaitSumUs);
        const auto wireSum = take(stats.txWireSumUs);
        const auto totalSum = take(stats.txTotalSumUs);
        return Stats{
            .txQueued = take(stats.txQueued),
            .txAcked = take(stats.txAcked),
            .txFailed = take(stats.txFailed),
            .txInFlightFull = take(stats.txInFlightFull),
            .txSerializeDropped = take(stats.txSerializeDropped),
            .rxQueued = take(stats.rxQueued),
            .rxDropped = take(stats.rxDropped),
            .txTimingSamples = timingSamples,
            .txQueueWaitMinUs = takeMin(stats.txQueueWaitMinUs),
            .txQueueWaitAvgUs =
                timingSamples == 0 ? 0 : queueWaitSum / timingSamples,
            .txQueueWaitMaxUs = take(stats.txQueueWaitMaxUs),
            .txWireMinUs = takeMin(stats.txWireMinUs),
            .txWireAvgUs = timingSamples == 0 ? 0 : wireSum / timingSamples,
            .txWireMaxUs = take(stats.txWireMaxUs),
            .txTotalMinUs = takeMin(stats.txTotalMinUs),
            .txTotalAvgUs = timingSamples == 0 ? 0 : totalSum / timingSamples,
            .txTotalMaxUs = take(stats.txTotalMaxUs),
        };
    }

    [[nodiscard]] detail::PeerMask _configuredPeerMask() const {
        detail::PeerMask out = 0;
        for (const auto &peer : _peers) {
            out |= peer.peerId;
        }
        return out;
    }

    [[nodiscard]] uint8_t _countTargetPeers(detail::PeerMask targetPeers) const {
        auto count = uint8_t{0};
        for (const auto &peer : _peers) {
            if ((targetPeers & peer.peerId) != 0) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] detail::PeerMask _availablePeers() const {
        detail::PeerMask out = 0;
        for (const auto &peer : _peers) {
            if (peer.link != nullptr && peer.link->ready()) {
                out |= peer.peerId;
            }
        }
        return out;
    }

    [[nodiscard]] Peer *_peerById(PeerId peerId) {
        for (auto &peer : _peers) {
            if (peer.peerId == peerId) {
                return &peer;
            }
        }
        return nullptr;
    }

    ReturnCode _observePeerAvailability() {
        const auto availablePeers = _availablePeers();
        const auto newlyAvailablePeers =
            static_cast<detail::PeerMask>(availablePeers & ~_knownAvailablePeers);
        const bool wasAvailable = _knownAvailablePeers != 0;
        const bool isAvailable = availablePeers != 0;
        if (availablePeers == _knownAvailablePeers) {
            return OK();
        }
        _knownAvailablePeers = availablePeers;
        if (_availabilityObserver != nullptr) {
            if (newlyAvailablePeers != 0) {
                FAIL_IF_ERR_FWD(
                    _availabilityObserver->onTransportAvailabilityChanged(
                        _transportId, true),
                    "Failed to notify shared SPI peer availability");
            } else if (wasAvailable && !isAvailable) {
                FAIL_IF_ERR_FWD(
                    _availabilityObserver->onTransportAvailabilityChanged(
                        _transportId, false),
                    "Failed to notify shared SPI peer loss");
            }
        }
        return _wake();
    }

    static ReturnCode _onWireData(void *owner,
                                  Wire::PayloadType /*payloadType*/,
                                  std::span<const std::byte> payload,
                                  int64_t /*receivedAtUs*/) {
        auto *peer = static_cast<Peer *>(owner);
        FAIL_IF_NULL(peer, ERR(CoreError, InvalidArgument),
                     "SPI router peer owner is null");
        return peer->transport->_receiveWireData(*peer, payload);
    }

    ReturnCode _receiveWireData(Peer &peer, std::span<const std::byte> payload) {
        if (!peer.link->ready()) {
            detail::metrics().addSpiDrop();
            peer.stats.rxDropped.fetch_add(1, std::memory_order_relaxed);
            _log_w(SV_FMT ": rejecting SPI PubSub frame from " SV_FMT
                   " while link is not ready",
                   SV_ARG(_instanceName), SV_ARG(peer.name));
            return ERR(CoreError, InvalidState);
        }
        if (payload.size() > bufferSize) {
            detail::metrics().addSpiDrop();
            peer.stats.rxDropped.fetch_add(1, std::memory_order_relaxed);
            _log_w(SV_FMT ": dropping oversized SPI PubSub frame from "
                   SV_FMT " of %zu bytes",
                   SV_ARG(_instanceName), SV_ARG(peer.name), payload.size());
            return OK();
        }
        auto headerResult = detail::SerDe::tryPeekHeader(payload);
        if (!headerResult) {
            detail::metrics().addSpiDrop();
            peer.stats.rxDropped.fetch_add(1, std::memory_order_relaxed);
            _log_w(SV_FMT ": dropping invalid SPI PubSub frame from " SV_FMT
                   " of %zu bytes: " ERR_FMT,
                   SV_ARG(_instanceName), SV_ARG(peer.name), payload.size(),
                   ERR_ARG(headerResult.error()));
            return OK();
        }
        auto validateRet = detail::SerDe::tryValidateFrame(payload,
                                                           *headerResult);
        if (!validateRet.ok()) {
            detail::metrics().addSpiDrop();
            peer.stats.rxDropped.fetch_add(1, std::memory_order_relaxed);
            _log_w(SV_FMT ": dropping corrupt SPI PubSub frame from " SV_FMT
                   " of %zu bytes: " ERR_FMT,
                   SV_ARG(_instanceName), SV_ARG(peer.name), payload.size(),
                   ERR_ARG(validateRet));
            return OK();
        }

        RxFrame rxFrame{};
        std::memcpy(rxFrame.data.data(), payload.data(), payload.size());
        rxFrame.size = payload.size();
        detail::log_trace_frame("spi.router.ingress", payload,
                                detail::SerDe::peekHeader,
                                _instanceName.data());
        auto sendRet = Totem::Queue::Platform::send(
            peer.rxFrameQueue, &rxFrame, queueSendTimeoutTicks);
        if (!sendRet.ok()) {
            detail::metrics().addSpiDrop();
            peer.stats.rxDropped.fetch_add(1, std::memory_order_relaxed);
            if (sendRet == ERR(Timeout) || sendRet == ERR(CoreError, Timeout) ||
                sendRet == ERR(Overflow) || sendRet == ERR(CoreError, Overflow)) {
                _log_w(SV_FMT ": dropping SPI PubSub frame from " SV_FMT
                       " after RX queue backpressure: " ERR_FMT,
                       SV_ARG(_instanceName), SV_ARG(peer.name),
                       ERR_ARG(sendRet));
                return OK();
            }
            FAIL(sendRet, "Failed to enqueue shared SPI ingress frame");
        }
        detail::metrics().addSpiRx();
        peer.stats.rxQueued.fetch_add(1, std::memory_order_relaxed);
        auto wakeRet = _wake();
        if (!wakeRet.ok()) {
            _log_w(SV_FMT
                   ": accepted SPI PubSub frame from " SV_FMT
                   " but failed to wake PubSub: " ERR_FMT,
                   SV_ARG(_instanceName), SV_ARG(peer.name), ERR_ARG(wakeRet));
        }
        return OK();
    }

    std::expected<size_t, ReturnCode>
    _prepareFrameForSend(detail::FrameHandle frameHandle,
                         std::span<std::byte> sendBuffer) {
        FAIL_IF(!frameHandle || frameHandle->pendingCount == 0 ||
                    !frameHandle->envelope.valid(),
                std::unexpected(ERR(CoreError, InvalidState)),
                "Cannot prepare invalid queued SPI router frame handle");
        const auto frameSize =
            detail::SerDe::encodedSize(frameHandle->envelope.header);
        if (frameSize > sendBuffer.size()) {
            return std::unexpected(ERR(CoreError, Overflow));
        }

        if (frameHandle->envelope.owner == &_ingress &&
            _ingress.hasSerializedFrame(frameHandle->envelope.header)) {
            FAIL_IF_ERR_FWD_UNEXPECTED(
                _ingress.getSerializedFrame(
                    frameHandle->envelope.header,
                    std::span<std::byte>{sendBuffer.data(), frameSize}),
                "Failed to reuse serialized ingress frame for shared SPI "
                "fanout");
        } else {
            FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
                encodedSize,
                detail::SerDe::serialize(frameHandle->envelope, sendBuffer),
                "Failed to serialize shared SPI router frame");
            (void)encodedSize;
        }
        return frameSize;
    }

    ReturnCode _queuePeerWrite(Peer &peer, const Header &header,
                               std::span<const std::byte> frame,
                               PendingFrame *pending,
                               bool acknowledgeOnComplete) {
        if (!peer.link->ready()) {
            return ERR(CoreError, InvalidState);
        }
        auto *slot = _findFreeInFlight(peer);
        if (slot == nullptr) {
            detail::metrics().addSpiDrop();
            peer.stats.txInFlightFull.fetch_add(1, std::memory_order_relaxed);
            return ERR(CoreError, Overflow);
        }

        return _startPeerWrite(peer, header, frame, pending,
                               acknowledgeOnComplete, *slot);
    }

    ReturnCode _startPeerWrite(Peer &peer, const Header &header,
                               std::span<const std::byte> frame,
                               PendingFrame *pending,
                               bool acknowledgeOnComplete,
                               InFlightFrame &slot) {
        std::memcpy(slot.data.data(), frame.data(), frame.size());
        slot.header = header;
        slot.size = frame.size();
        slot.transport = this;
        slot.peer = &peer;
        slot.pending = pending;
        slot.queuedAtUs = ::platform::get_time_us();
        slot.occupied = true;
        slot.acknowledgeOnComplete = acknowledgeOnComplete;

        auto request = Wire::WriteRequest{
            .owner = &slot,
            .payloadType = Wire::PayloadType::PubSub,
            .data = std::span<const std::byte>{slot.data.data(), slot.size},
            .onComplete = _onWireWriteComplete,
        };
        auto sendRet = peer.link->send(request);
        if (!sendRet.ok()) {
            slot = {};
            detail::metrics().addSpiFail();
            peer.stats.txFailed.fetch_add(1, std::memory_order_relaxed);
            return sendRet;
        }
        detail::metrics().addSpiTx();
        peer.stats.txQueued.fetch_add(1, std::memory_order_relaxed);
        return OK();
    }

    ReturnCode _sendRawFrame(Peer &peer, const Header &header,
                             std::span<const std::byte> frame) {
        FAIL_IF(frame.size() > bufferSize, ERR(CoreError, Overflow),
                "Raw shared SPI PubSub frame exceeds transport buffer size");
        if (peer.rawSendQueued.load(std::memory_order_relaxed) != 0) {
            return _queueRawFrame(peer, header, frame);
        }

        auto *slot = _findFreeInFlight(peer);
        if (slot == nullptr) {
            peer.stats.txInFlightFull.fetch_add(1,
                                                std::memory_order_relaxed);
            return _queueRawFrame(peer, header, frame);
        }

        return _startRawFrame(peer, header, frame, *slot);
    }

    ReturnCode _queueRawFrame(Peer &peer, const Header &header,
                              std::span<const std::byte> frame) {
        FAIL_IF_ERR_FWD(_ensurePeerRawSendQueue(peer),
                        "Failed to ensure shared SPI raw send queue for "
                        SV_FMT,
                        SV_ARG(peer.name));
        RawFrame rawFrame{};
        rawFrame.header = header;
        std::memcpy(rawFrame.data.data(), frame.data(), frame.size());
        rawFrame.size = frame.size();

        if (Totem::Queue::Platform::spacesAvailable(peer.rawSendQueue) == 0) {
            detail::metrics().addSpiDrop();
            peer.stats.txInFlightFull.fetch_add(1,
                                                std::memory_order_relaxed);
            return ERR(CoreError, Overflow);
        }

        auto sendRet =
            Totem::Queue::Platform::send(peer.rawSendQueue, &rawFrame, 0);
        if (!sendRet.ok()) {
            detail::metrics().addSpiDrop();
            peer.stats.txInFlightFull.fetch_add(1,
                                                std::memory_order_relaxed);
            return sendRet;
        }
        peer.rawSendQueued.fetch_add(1, std::memory_order_relaxed);
        return OK();
    }

    ReturnCode _sendDeferredRawFrame(bool &sent) {
        sent = false;
        for (size_t scanned = 0; scanned < PeerCount; ++scanned) {
            const auto index = (_sendCursor + scanned) % PeerCount;
            auto &peer = _peers[index];
            FAIL_IF_ERR_FWD(_sendDeferredRawFrame(peer, sent),
                            "Failed to send deferred raw shared SPI frame to "
                            SV_FMT,
                            SV_ARG(peer.name));
            if (sent) {
                _sendCursor = (index + 1) % PeerCount;
                return OK();
            }
        }
        return OK();
    }

    ReturnCode _sendDeferredRawFrame(Peer &peer, bool &sent) {
        sent = false;
        if (peer.rawSendQueued.load(std::memory_order_relaxed) == 0) {
            return OK();
        }

        auto *slot = _findFreeInFlight(peer);
        if (slot == nullptr) {
            peer.stats.txInFlightFull.fetch_add(1,
                                                std::memory_order_relaxed);
            return OK();
        }

        RawFrame rawFrame{};
        auto receiveRet =
            Totem::Queue::Platform::receive(peer.rawSendQueue, &rawFrame, 0);
        if (!receiveRet.ok()) {
            if (receiveRet == ERR(Timeout)) {
                return OK();
            }
            FAIL(receiveRet,
                 "Failed to receive deferred raw shared SPI frame: " ERR_FMT,
                 ERR_ARG(receiveRet));
        }
        peer.rawSendQueued.fetch_sub(1, std::memory_order_relaxed);
        auto sendRet = _startRawFrame(
            peer, rawFrame.header,
            std::span<const std::byte>{rawFrame.data.data(), rawFrame.size},
            *slot);
        if (!sendRet.ok()) {
            _log_w(SV_FMT
                   ": dropping deferred raw shared SPI PubSub message %u to "
                   SV_FMT " after enqueue failed: " ERR_FMT,
                   SV_ARG(_instanceName), rawFrame.header.messageId,
                   SV_ARG(peer.name), ERR_ARG(sendRet));
        }
        sent = true;
        return OK();
    }

    ReturnCode _startRawFrame(Peer &peer, const Header &header,
                              std::span<const std::byte> frame,
                              InFlightFrame &slot) {
        return _startPeerWrite(peer, header, frame, nullptr, false, slot);
    }

    static ReturnCode _onWireWriteComplete(Wire::WriteResult result) {
        auto *slot = static_cast<InFlightFrame *>(result.owner);
        if (slot == nullptr || slot->transport == nullptr) {
            return OK();
        }
        return slot->transport->_completeWireWrite(*slot, result);
    }

    ReturnCode _completeWireWrite(InFlightFrame &slot,
                                  Wire::WriteResult result) {
        if (!slot.occupied || slot.peer == nullptr) {
            return OK();
        }

        auto *peer = slot.peer;
        auto *pending = slot.pending;
        const auto header = slot.header;
        const auto shouldAck = slot.acknowledgeOnComplete;
        _recordTxTiming(*peer, slot, result);
        slot = {};

        if (!result.result.ok()) {
            detail::metrics().addSpiFail();
            peer->stats.txFailed.fetch_add(1, std::memory_order_relaxed);
            if (result.result == ERR(CoreError, Timeout)) {
                _log_v(SV_FMT ": SPI write to " SV_FMT
                       " timed out for PubSub message %u: " ERR_FMT,
                       SV_ARG(_instanceName), SV_ARG(peer->name),
                       header.messageId, ERR_ARG(result.result));
            } else {
                _log_w(SV_FMT ": SPI write to " SV_FMT
                       " failed for PubSub message %u: " ERR_FMT,
                       SV_ARG(_instanceName), SV_ARG(peer->name),
                       header.messageId, ERR_ARG(result.result));
            }
        } else {
            detail::metrics().addSpiAck();
            peer->stats.txAcked.fetch_add(1, std::memory_order_relaxed);
        }

        if (!shouldAck || pending == nullptr) {
            return OK();
        }

        Envelope ackEnvelope{};
        if (!_releasePendingTarget(*pending, ackEnvelope)) {
            return OK();
        }
        auto ackRet = _ack(ackEnvelope);
        if (!ackRet.ok()) {
            _log_w(SV_FMT
                   ": completed shared SPI message %u was not tracked for "
                   "release: " ERR_FMT,
                   SV_ARG(_instanceName), ackEnvelope.header.messageId,
                   ERR_ARG(ackRet));
            if (ackRet == ERR(CoreError, NotFound)) {
                return OK();
            }
        }
        return ackRet;
    }

    PendingFrame *_reservePending(const Envelope &envelope,
                                  uint8_t pendingCount) {
        Mutex::ScopedSpinlockGuard guard{_pendingLock};
        for (auto &pending : _pendingFrames) {
            if (!pending.occupied) {
                pending.envelope = envelope;
                pending.pendingCount = pendingCount;
                pending.occupied = true;
                return &pending;
            }
        }
        return nullptr;
    }

    bool _releasePendingTarget(PendingFrame &pending, Envelope &ackEnvelope) {
        Mutex::ScopedSpinlockGuard guard{_pendingLock};
        if (!pending.occupied || pending.pendingCount == 0) {
            return false;
        }
        --pending.pendingCount;
        if (pending.pendingCount != 0) {
            return false;
        }
        ackEnvelope = pending.envelope;
        pending = {};
        return true;
    }

    InFlightFrame *_findFreeInFlight(Peer &peer) {
        for (auto &slot : peer.inFlight) {
            if (!slot.occupied) {
                return &slot;
            }
        }
        return nullptr;
    }

    ReturnCode _ack(const Envelope &envelope) {
        return _sendAckCallback(_pubSubNode, _transportId, envelope);
    }

    ReturnCode _wake(Signal signal = Signal::Ping) {
        if (_wakeCallback == nullptr) {
            return OK();
        }
        return _wakeCallback(_pubSubNode, signal);
    }

    void _recordTxTiming(Peer &peer, const InFlightFrame &slot,
                         const Wire::WriteResult &result) {
        if (!result.result.ok() || slot.queuedAtUs <= 0 ||
            result.sentAtUs <= 0 || result.completedAtUs <= 0 ||
            result.sentAtUs < slot.queuedAtUs ||
            result.completedAtUs < result.sentAtUs) {
            return;
        }

        const auto queueWaitUs =
            static_cast<uint32_t>(result.sentAtUs - slot.queuedAtUs);
        const auto wireUs =
            static_cast<uint32_t>(result.completedAtUs - result.sentAtUs);
        const auto totalUs =
            static_cast<uint32_t>(result.completedAtUs - slot.queuedAtUs);

        peer.stats.txTimingSamples.fetch_add(1, std::memory_order_relaxed);
        _recordTimingValue(peer.stats.txQueueWaitSumUs,
                           peer.stats.txQueueWaitMinUs,
                           peer.stats.txQueueWaitMaxUs, queueWaitUs);
        _recordTimingValue(peer.stats.txWireSumUs, peer.stats.txWireMinUs,
                           peer.stats.txWireMaxUs, wireUs);
        _recordTimingValue(peer.stats.txTotalSumUs, peer.stats.txTotalMinUs,
                           peer.stats.txTotalMaxUs, totalUs);
    }

    static void _recordTimingValue(std::atomic<uint32_t> &sum,
                                   std::atomic<uint32_t> &min,
                                   std::atomic<uint32_t> &max,
                                   uint32_t value) {
        sum.fetch_add(value, std::memory_order_relaxed);

        auto currentMin = min.load(std::memory_order_relaxed);
        while (value < currentMin &&
               !min.compare_exchange_weak(currentMin, value,
                                          std::memory_order_relaxed,
                                          std::memory_order_relaxed)) {
        }

        auto currentMax = max.load(std::memory_order_relaxed);
        while (value > currentMax &&
               !max.compare_exchange_weak(currentMax, value,
                                          std::memory_order_relaxed,
                                          std::memory_order_relaxed)) {
        }
    }

    ReturnCode _onBegin() {
        auto ret = OK();
        for (auto &peer : _peers) {
            if (peer.rxFrameQueue != nullptr) {
                continue;
            }
            FAIL_IF_UNEXPECTED_FWD(
                queueHandle, Totem::Queue::Platform::create(peer.rxStorage),
                "Failed to create shared SPI RX queue for " SV_FMT,
                SV_ARG(peer.name));
            peer.rxFrameQueue = queueHandle;
        }
        return ret;
    }

    ReturnCode _ensurePeerRawSendQueue(Peer &peer) {
        if (peer.rawSendQueue != nullptr) {
            return OK();
        }
        auto queueResult =
            Totem::Queue::Platform::create(peer.rawSendStorage);
        FAIL_IF_UNEXPECTED_FWD(
            queueHandle, queueResult,
            "Failed to create shared SPI raw send queue for " SV_FMT,
            SV_ARG(peer.name));
        peer.rawSendQueue = queueHandle;
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        for (auto &peer : _peers) {
            if (peer.rawSendQueue != nullptr) {
                ret.combine(
                    Totem::Queue::Platform::destroy(peer.rawSendQueue));
                peer.rawSendQueue = {};
                peer.rawSendQueued.store(0, std::memory_order_relaxed);
            }
            if (peer.rxFrameQueue != nullptr) {
                ret.combine(
                    Totem::Queue::Platform::destroy(peer.rxFrameQueue));
                peer.rxFrameQueue = {};
            }
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
    std::array<Peer, PeerCount> _peers{};
    std::array<PendingFrame, pendingDepth> _pendingFrames{};
    detail::PeerMask _knownAvailablePeers = 0;
    size_t _receiveCursor = 0;
    size_t _sendCursor = 0;
    ::platform::Spinlock _pendingLock = ::platform::create_spinlock();

    static constexpr ::platform::Tick queueSendTimeoutTicks = 1;
};

template <class Link, size_t PeerCount>
inline constexpr LifecycleContract<SpiRouterTransport<Link, PeerCount>>
    _spi_router_transport_lifecycle_contract;

} // namespace Totem::PubSubBackend::Transports
