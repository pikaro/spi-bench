#pragma once

#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/detail/ITransport.hpp"
#include "PubSubBackend/detail/IngressBuffer.hpp"
#include "PubSubBackend/detail/SerDe.hpp"
#include "PubSubBackend/detail/Trace.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Queue/Facade.hpp"
#include "Types/Error.hpp"
#include "Types/Signal.hpp"
#include <array>
#include <cstddef>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace Totem::PubSubBackend::Transports {

constexpr auto logComponent = PubSubBackend::detail::logComponent;

using SendAckCallback = ReturnCode (*)(void *owner,
                                       detail::TransportId transportId,
                                       const Envelope &envelope);
using SendCallback = ReturnCode (*)(void *owner, const Header &header,
                                    std::span<const std::byte> frame);
using ReceiveCallback = std::expected<size_t, ReturnCode> (*)(
    void *owner, std::span<std::byte> out);
using AvailableCallback = bool (*)(void *owner);
using WakeCallback = ReturnCode (*)(void *owner, Signal signal);
using IngressDispatchCallback = detail::IngressDispatchCallback;

struct BaseTransportDependencies {
    void *pubSubNode;
    void *transport = nullptr;
    detail::TransportId transportId;
    detail::TransportForwardingPolicy forwardingPolicy =
        detail::TransportForwardingPolicy::PointToPoint;
    std::string_view name;
    SendAckCallback sendAckCallback;
    SendCallback sendCallback = nullptr;
    ReceiveCallback receiveCallback = nullptr;
    AvailableCallback availableCallback = nullptr;
    detail::ITransportAvailabilityObserver *availabilityObserver = nullptr;
    WakeCallback wakeCallback = nullptr;
    IngressDispatchCallback ingressDispatchCallback = nullptr;
    detail::IngressBuffer &ingress;

    [[nodiscard]] bool valid() const {
        return pubSubNode != nullptr && transport != nullptr &&
               sendAckCallback != nullptr && receiveCallback != nullptr &&
               sendCallback != nullptr && !name.empty();
    }
};

class BaseTransport : public HasLifecycle<BaseTransport>,
                      public detail::ITransport {
    friend class HasLifecycle<BaseTransport>;
    friend struct LifecycleContract<BaseTransport>;
    using Topic = typename detail::Spec::Topic;
    using NodeId = typename detail::Spec::NodeId;

  public:
    explicit BaseTransport(const BaseTransportDependencies &deps)
        : _pubSubNode(deps.pubSubNode), _transport(deps.transport),
          _instanceName(deps.name), _transportId(deps.transportId),
          _forwardingPolicy(deps.forwardingPolicy),
          _sendAckCallback(deps.sendAckCallback),
          _sendCallback(deps.sendCallback),
          _receiveCallback(deps.receiveCallback),
          _availableCallback(deps.availableCallback),
          _availabilityObserver(deps.availabilityObserver),
          _wakeCallback(deps.wakeCallback),
          _ingressDispatchCallback(deps.ingressDispatchCallback),
          _ingress(deps.ingress) {
        ABORT_IF_NOT(deps.valid(), "Invalid BaseTransport dependencies");
    }

    DELETE_COPY(BaseTransport)
    DELETE_MOVE(BaseTransport)

    static constexpr const char *name = "BaseTransport";

    [[nodiscard]] detail::TransportId transportId() const override {
        return _transportId;
    }
    [[nodiscard]] std::string_view instanceName() const override {
        return _instanceName;
    }
    [[nodiscard]] detail::TransportForwardingPolicy
    forwardingPolicy() const override {
        return _forwardingPolicy;
    }
    [[nodiscard]] detail::PeerMask knownPeers() const override { return 0; }
    [[nodiscard]] bool available() const override { return _available(); }

    ReturnCode
    enqueue(detail::FrameHandle frameHandle,
            const detail::TransportDispatch & /*dispatch*/ = {}) override {
        FAIL_IF_ERR_FWD(_observeAvailability(),
                        "Failed to observe transport availability");
        FAIL_IF_NOT(_available(), ERR(InvalidState),
                    "Cannot enqueue frame for unavailable transport " SV_FMT,
                    SV_ARG(_instanceName));
        detail::log_trace_packet("transport.enqueue",
                                 frameHandle->envelope.header,
                                 _instanceName.data());
        _log_d(SV_FMT ": enqueue send for " MAGIC_PUBSUB_SV_FMT,
               SV_ARG(_instanceName),
               MAGIC_PUBSUB_SV_ARG(frameHandle->envelope.header));
        FAIL_IF_ERR_FWD(Totem::Queue::Platform::send(
                            _sendQueue, static_cast<void *>(&frameHandle)),
                        "Failed to enqueue frame for sending");
        return OK();
    }

    ReturnCode
    enqueueRaw(const Header &header, std::span<const std::byte> frame,
               const detail::TransportDispatch & /*dispatch*/ = {}) override {
        FAIL_IF_ERR_FWD(_observeAvailability(),
                        "Failed to observe transport availability");
        FAIL_IF_NOT(_available(), ERR(InvalidState),
                    "Cannot enqueue raw frame for unavailable transport "
                    SV_FMT,
                    SV_ARG(_instanceName));
        _log_d(SV_FMT ": direct raw enqueue for " MAGIC_PUBSUB_SV_FMT,
               SV_ARG(_instanceName), MAGIC_PUBSUB_SV_ARG(header));
        return _sendCallback(_transport, header, frame);
    }

    ReturnCode
    send(size_t maxCount = std::numeric_limits<size_t>::max()) override {
        FAIL_IF_INACTIVE_ERR("Cannot work inactive transport %s",
                             _instanceName);
        FAIL_IF_ERR_FWD(_observeAvailability(),
                        "Failed to observe transport availability");
        if (!_available()) {
            return OK();
        }
        auto ret = OK();
        size_t count = 0;

        detail::FrameHandle item;

        while (ret.ok() && count < maxCount) {
            ret.combine(Totem::Queue::Platform::receive(
                _sendQueue, static_cast<void *>(&item), 0));
            if (ret.ok()) {
                detail::log_trace_packet("transport.send.dequeue",
                                         item->envelope.header,
                                         _instanceName.data());
                _log_d(SV_FMT ": dequeued send for " MAGIC_PUBSUB_SV_FMT,
                       SV_ARG(_instanceName),
                       MAGIC_PUBSUB_SV_ARG(item->envelope.header));
                // Keep transport scratch frames on the stack. They are small,
                // moving them into shared transport state did not lower the
                // observed PubSub stack watermark, and member buffers add
                // refactoring risk if transport call ownership ever widens
                // beyond the current single-runner model.
                std::array<std::byte, bufferSize> sendBuffer;
                auto frameSizeResult = _prepareFrameForSend(item, sendBuffer);
                if (!frameSizeResult) {
                    _log_w(SV_FMT
                           ": dropping unserializable PubSub message %u: "
                           ERR_FMT,
                           SV_ARG(_instanceName),
                           item == nullptr ? 0
                                           : item->envelope.header.messageId,
                           ERR_ARG(frameSizeResult.error()));
                    if (_canAckFrameHandle(item)) {
                        auto ackRet = _ack(item->envelope);
                        if (!ackRet.ok()) {
                            _log_w(SV_FMT
                                   ": dropped PubSub message %u was not "
                                   "tracked for release: " ERR_FMT,
                                   SV_ARG(_instanceName),
                                   item->envelope.header.messageId,
                                   ERR_ARG(ackRet));
                        }
                    }
                    ++count;
                    continue;
                }
                const auto frameSize = *frameSizeResult;
                auto frame =
                    std::span<const std::byte>{sendBuffer.data(), frameSize};
                detail::log_trace_packet("transport.send.bytes",
                                         item->envelope.header,
                                         _instanceName.data());
                _log_d(SV_FMT ": sending %zu bytes for " MAGIC_PUBSUB_SV_FMT,
                       SV_ARG(_instanceName), frameSize,
                       MAGIC_PUBSUB_SV_ARG(item->envelope.header));
                FAIL_IF_ERR_FWD(
                    _sendCallback(_transport, item->envelope.header, frame),
                    "Failed to process frame from send queue");
                FAIL_IF_ERR_FWD(_ack(item->envelope),
                                "Failed to acknowledge frame with transport");
                ++count;
            }
        }
        if (!ret.ok()) {
            if (ret == ERR(Timeout)) {
                return OK();
            }
            FAIL(ret, "Failed to receive frame from send queue: " ERR_FMT,
                 ERR_ARG(ret));
        }
        return OK();
    }

    ReturnCode
    receive(size_t maxCount = std::numeric_limits<size_t>::max()) override {
        FAIL_IF_INACTIVE_ERR("Cannot work inactive transport %s",
                             _instanceName);
        FAIL_IF_ERR_FWD(_observeAvailability(),
                        "Failed to observe transport availability");
        if (!_available()) {
            return OK();
        }
        auto ret = OK();
        size_t count = 0;

        while (ret.ok() && count < maxCount) {
            std::array<std::byte, bufferSize> receiveBuffer;
            auto receiveResult = _receiveCallback(_transport, receiveBuffer);

            if (receiveResult) {
                _log_d(SV_FMT ": received raw frame of %zu bytes",
                       SV_ARG(_instanceName), *receiveResult);
                auto frame = std::span<const std::byte>{receiveBuffer.data(),
                                                        *receiveResult};
                auto headerResult = detail::SerDe::tryPeekHeader(frame);
                if (!headerResult) {
                    _log_w(SV_FMT
                           ": dropping invalid transport ingress frame "
                           "of %zu bytes: " ERR_FMT,
                           SV_ARG(_instanceName), frame.size(),
                           ERR_ARG(headerResult.error()));
                    ++count;
                    continue;
                }
                if (_ingressDispatchCallback != nullptr) {
                    auto dispatchResult = _ingressDispatchCallback(
                        _pubSubNode, frame,
                        detail::IngressContext{
                            .transportId = _transportId,
                        });
                    if (!dispatchResult) {
                        auto error = dispatchResult.error();
                        if (_isRecoverableIngressError(error)) {
                            _log_w(SV_FMT
                                   ": dropping invalid transport ingress "
                                   "frame of %zu bytes while dispatching: "
                                   ERR_FMT,
                                   SV_ARG(_instanceName), frame.size(),
                                   ERR_ARG(error));
                            ++count;
                            continue;
                        }
                        FAIL(error,
                             "Failed to dispatch transport ingress frame: "
                             ERR_FMT,
                             ERR_ARG(error));
                    }
                    auto handled = *dispatchResult;
                    if (handled) {
                        ++count;
                        continue;
                    }
                }
                auto storeResult = _ingress.storeFrame(frame);
                if (!storeResult) {
                    auto error = storeResult.error();
                    if (_isRecoverableIngressError(error)) {
                        _log_w(SV_FMT
                               ": dropping invalid transport ingress frame "
                               "of %zu bytes while storing: " ERR_FMT,
                               SV_ARG(_instanceName), frame.size(),
                               ERR_ARG(error));
                        ++count;
                        continue;
                    }
                    FAIL(error,
                         "Failed to store received frame in ingress: "
                         ERR_FMT,
                         ERR_ARG(error));
                }
                auto envelope = std::move(*storeResult);
                if (!envelope.has_value()) {
                    ++count;
                    continue;
                }
                detail::log_trace_packet("transport.rx.envelope",
                                         envelope->header,
                                         _instanceName.data());
                _log_d(SV_FMT
                       ": enqueuing received envelope for " MAGIC_PUBSUB_SV_FMT,
                       SV_ARG(_instanceName),
                       MAGIC_PUBSUB_SV_ARG(envelope->header));
                FAIL_IF_ERR_FWD(
                    Totem::Queue::Platform::send(
                        _publishQueue, static_cast<void *>(&*envelope)),
                    "Failed to enqueue received frame for publishing");
                ++count;
            } else {
                ret.combine(receiveResult.error());
            }
        }
        if (!ret.ok()) {
            if (ret == ERR(Timeout)) {
                return OK();
            }
            FAIL(ret, "Failed to receive frame with transport: " ERR_FMT,
                 ERR_ARG(ret));
        }
        return OK();
    }

    ReturnCode
    pollInto(void *ctx, detail::PollIntoCallback callback,
             size_t maxCount = std::numeric_limits<size_t>::max()) override {
        FAIL_IF_ERR_FWD(receive(maxCount),
                        "Failed to receive transport ingress before polling");
        FAIL_IF_ERR_FWD(_observeAvailability(),
                        "Failed to observe transport availability");
        if (!_available()) {
            return OK();
        }
        size_t count = 0;
        while (count++ < maxCount) {
            Envelope item;
            auto receiveRet = Totem::Queue::Platform::receive(
                _publishQueue, static_cast<void *>(&item), 0);
            if (!receiveRet.ok()) {
                if (receiveRet == ERR(Timeout)) {
                    return OK();
                }
                FAIL(receiveRet,
                     "Failed to receive item from publish queue: " ERR_FMT,
                     ERR_ARG(receiveRet));
            }
            _log_d(SV_FMT ": pollInto dispatch " MAGIC_PUBSUB_SV_FMT,
                   SV_ARG(_instanceName), MAGIC_PUBSUB_SV_ARG(item.header));
            detail::log_trace_packet("transport.pollInto", item.header,
                                     _instanceName.data());
            FAIL_IF_ERR_FWD(callback(ctx, item, std::nullopt),
                            "Failed to process item from publish queue");
        }
        std::unreachable();
    }

  protected:
    std::expected<size_t, ReturnCode>
    _prepareFrameForSend(detail::FrameHandle frameHandle,
                         std::span<std::byte> sendBuffer) {
        FAIL_IF(!frameHandle || frameHandle->pendingCount == 0 ||
                    !frameHandle->envelope.valid(),
                std::unexpected(ERR(CoreError, InvalidState)),
                "Cannot prepare invalid queued frame handle");
        const auto frameSize =
            detail::SerDe::encodedSize(frameHandle->envelope.header);
        if (frameSize > sendBuffer.size()) {
            return std::unexpected(ERR(Overflow));
        }

        if (frameHandle->envelope.owner == &_ingress &&
            _ingress.hasSerializedFrame(frameHandle->envelope.header)) {
            FAIL_IF_ERR_FWD_UNEXPECTED(
                _ingress.getSerializedFrame(
                    frameHandle->envelope.header,
                    std::span<std::byte>{sendBuffer.data(), frameSize}),
                "Failed to reuse serialized ingress frame for sending");
        } else {
            FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
                encodedSize,
                detail::SerDe::serialize(frameHandle->envelope, sendBuffer),
                "Failed to serialize frame for sending");
            (void)encodedSize;
        }
        return frameSize;
    }

    [[nodiscard]] static bool _canAckFrameHandle(
        detail::FrameHandle frameHandle) {
        return frameHandle != nullptr &&
               frameHandle->envelope.header.messageId != 0 &&
               frameHandle->envelope.release != nullptr;
    }

    ReturnCode _ack(const Envelope &envelope) {
        detail::log_trace_packet("transport.ack", envelope.header,
                                 _instanceName.data());
        _log_d(SV_FMT ": ack " MAGIC_PUBSUB_SV_FMT, SV_ARG(_instanceName),
               MAGIC_PUBSUB_SV_ARG(envelope.header));
        return _sendAckCallback(_pubSubNode, _transportId, envelope);
    }

    ReturnCode _wake(Signal signal = Signal::Ping) {
        if (_wakeCallback == nullptr) {
            return OK();
        }
        return _wakeCallback(_pubSubNode, signal);
    }

    [[nodiscard]] bool _available() const {
        if (_availableCallback == nullptr) {
            return true;
        }
        return _availableCallback(_transport);
    }

    [[nodiscard]] static bool _isRecoverableIngressError(ReturnCode error) {
        return error == ERR(CoreError, InvalidData);
    }

    ReturnCode _observeAvailability() {
        const auto availableNow = _available();
        if (!_availabilityKnown) {
            _availabilityKnown = true;
            _wasAvailable = availableNow;
            if (!availableNow || _availabilityObserver == nullptr) {
                return OK();
            }
            return _availabilityObserver->onTransportAvailabilityChanged(
                _transportId, availableNow);
        }
        if (_wasAvailable == availableNow) {
            return OK();
        }
        _wasAvailable = availableNow;
        if (_availabilityObserver == nullptr) {
            return OK();
        }
        return _availabilityObserver->onTransportAvailabilityChanged(
            _transportId, availableNow);
    }

    ReturnCode _onBegin() {
        _log_i(SV_FMT ": begin transport", SV_ARG(_instanceName));
        auto sendQueueResult =
            Totem::Queue::Platform::create(_sendQueueStorage);
        FAIL_IF_ASSIGN_UNEXPECTED_FWD(
            _sendQueue, sendQueueResult,
            "Failed to create publish queue: " ERR_FMT,
            ERR_ARG(sendQueueResult.error()));
        auto publishQueueResult =
            Totem::Queue::Platform::create(_publishQueueStorage);
        FAIL_IF_ASSIGN_UNEXPECTED_FWD(
            _publishQueue, publishQueueResult,
            "Failed to create publish queue: " ERR_FMT,
            ERR_ARG(publishQueueResult.error()));
        return OK();
    }

    ReturnCode _onEnd() {
        _log_i(SV_FMT ": end transport", SV_ARG(_instanceName));
        auto ret = OK();
        if (_sendQueue != nullptr) {
            FAIL_IF_ERR_FWD(Totem::Queue::Platform::destroy(_sendQueue),
                            "Failed to destroy publish queue");
            _sendQueue = {};
        }
        if (_publishQueue != nullptr) {
            FAIL_IF_ERR_FWD(Totem::Queue::Platform::destroy(_publishQueue),
                            "Failed to destroy publish queue");
            _publishQueue = {};
        }
        return ret;
    }

    void *_pubSubNode = nullptr;
    void *_transport = nullptr;
    std::string_view _instanceName;
    detail::TransportId _transportId;
    detail::TransportForwardingPolicy _forwardingPolicy;
    SendAckCallback _sendAckCallback = nullptr;
    SendCallback _sendCallback = nullptr;
    ReceiveCallback _receiveCallback = nullptr;
    AvailableCallback _availableCallback = nullptr;
    detail::ITransportAvailabilityObserver *_availabilityObserver = nullptr;
    WakeCallback _wakeCallback = nullptr;
    IngressDispatchCallback _ingressDispatchCallback = nullptr;
    bool _availabilityKnown = false;
    bool _wasAvailable = false;

    Totem::Queue::Handle _sendQueue{};
    Totem::Queue::Platform::Storage<detail::FrameHandle,
                                    detail::Spec::Limits::maxMessageQueueSize>
        _sendQueueStorage{};

    Totem::Queue::Handle _publishQueue{};
    Totem::Queue::Platform::Storage<Envelope,
                                    detail::Spec::Limits::maxMessageQueueSize>
        _publishQueueStorage{};

    detail::IngressBuffer &_ingress;

    static constexpr auto bufferSize = detail::SerDe::headerSize +
                                       detail::Spec::Limits::maxPayloadSize +
                                       detail::SerDe::overheadSize;

}; // namespace Totem::PubSub::detail

inline constexpr LifecycleContract<BaseTransport>
    _base_transport_lifecycle_contract;

} // namespace Totem::PubSubBackend::Transports
