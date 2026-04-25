#pragma once

#include "BaseTransport.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/Interfaces/Wire.hpp"
#include "PubSubBackend/detail/EgressBuffer.hpp"
#include "PubSubBackend/detail/Metrics.hpp"
#include "PubSubBackend/detail/SerDe.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Queue/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string_view>
#include <utility>

namespace Totem::PubSubBackend::Transports {

class LocalSharedBusLink {
    static constexpr size_t frameBufferSize = detail::SerDe::headerSize +
                                             detail::Spec::Limits::maxPayloadSize +
                                             detail::SerDe::overheadSize;
    // A shared-bus link only connects one edge peer and one router. Keeping a
    // full PubSub-sized queue in each direction is disproportionately
    // expensive in static memory and is not representative of the intended
    // low-latency bus behavior.
    static constexpr size_t queueDepth = 4;

    struct Frame {
        std::array<std::byte, frameBufferSize> data;
        size_t size = 0;
    };

  public:
    explicit LocalSharedBusLink(std::string_view name) : _name(name) {}

    static constexpr const char *className = "LocalSharedBusLink";

    ReturnCode begin() {
        FAIL_IF_ERR_FWD(_ensureRouterQueue(),
                        "Failed to create shared-bus router queue for " SV_FMT,
                        SV_ARG(_name));
        FAIL_IF_ERR_FWD(_ensureEdgeQueue(),
                        "Failed to create shared-bus edge queue for " SV_FMT,
                        SV_ARG(_name));
        return OK();
    }

    [[nodiscard]] std::string_view name() const { return _name; }

    ReturnCode sendToRouter(std::span<const std::byte> frame) {
        FAIL_IF_ERR_FWD(begin(), "Failed to begin shared-bus link");
        return _send(_routerQueue, frame, "router");
    }

    std::expected<size_t, ReturnCode> receiveForRouter(std::span<std::byte> out) {
        FAIL_IF_ERR_FWD_UNEXPECTED(begin(), "Failed to begin shared-bus link");
        return _receive(_routerQueue, out, "router");
    }

    ReturnCode sendToEdge(std::span<const std::byte> frame) {
        FAIL_IF_ERR_FWD(begin(), "Failed to begin shared-bus link");
        return _send(_edgeQueue, frame, "edge");
    }

    std::expected<size_t, ReturnCode> receiveForEdge(std::span<std::byte> out) {
        FAIL_IF_ERR_FWD_UNEXPECTED(begin(), "Failed to begin shared-bus link");
        return _receive(_edgeQueue, out, "edge");
    }

  private:
    ReturnCode _send(Totem::Queue::Handle queue, std::span<const std::byte> frame,
                     const char *destination) {
        FAIL_IF_NULL(queue, ERR(InvalidState),
                     "Shared-bus link queue for %s is not available",
                     destination);
        Frame buffered;
        FAIL_IF(frame.size() > buffered.data.size(), ERR(InvalidArgument),
                "Frame size exceeds shared-bus link capacity for " SV_FMT,
                SV_ARG(_name));
        std::memcpy(buffered.data.data(), frame.data(), frame.size());
        buffered.size = frame.size();
        FAIL_IF_ERR_FWD(Totem::Queue::Platform::send(queue, &buffered,
                                                     queueSendTimeoutTicks),
                        "Failed to enqueue shared-bus link frame for %s",
                        destination);
        return OK();
    }

    std::expected<size_t, ReturnCode> _receive(Totem::Queue::Handle queue,
                                               std::span<std::byte> out,
                                               const char *source) {
        FAIL_IF_NULL(queue, std::unexpected(ERR(InvalidState)),
                     "Shared-bus link queue for %s is not available", source);
        Frame buffered;
        auto receiveRet = Totem::Queue::Platform::receive(queue, &buffered, 0);
        if (!receiveRet.ok()) {
            if (receiveRet == ERR(Timeout)) {
                return std::unexpected(ERR(Timeout));
            }
            FAIL(std::unexpected(receiveRet),
                 "Failed to dequeue shared-bus link frame for %s: " ERR_FMT,
                 source, ERR_ARG(receiveRet));
        }
        FAIL_IF(out.size() < buffered.size,
                std::unexpected(ERR(InvalidArgument)),
                "Output buffer too small for shared-bus link frame from %s",
                source);
        std::memcpy(out.data(), buffered.data.data(), buffered.size);
        return buffered.size;
    }

    ReturnCode _ensureRouterQueue() {
        if (_routerQueue != nullptr) {
            return OK();
        }
        auto queueResult = Totem::Queue::Platform::create(_routerQueueStorage);
        FAIL_IF_UNEXPECTED_FWD(queueHandle, std::move(queueResult),
                               "Failed to create shared-bus router queue");
        _routerQueue = queueHandle;
        return OK();
    }

    ReturnCode _ensureEdgeQueue() {
        if (_edgeQueue != nullptr) {
            return OK();
        }
        auto queueResult = Totem::Queue::Platform::create(_edgeQueueStorage);
        FAIL_IF_UNEXPECTED_FWD(queueHandle, std::move(queueResult),
                               "Failed to create shared-bus edge queue");
        _edgeQueue = queueHandle;
        return OK();
    }

    std::string_view _name;
    Totem::Queue::Handle _routerQueue{};
    Totem::Queue::Platform::Storage<Frame, queueDepth> _routerQueueStorage{};
    Totem::Queue::Handle _edgeQueue{};
    Totem::Queue::Platform::Storage<Frame, queueDepth> _edgeQueueStorage{};

    static constexpr ::platform::Tick queueSendTimeoutTicks = 1;
};

struct LocalSharedBusEdgeTransportDependencies {
    BaseTransportDependencies base;
    PeerId peerId;
    uint32_t readyAfterMs = 0;

    [[nodiscard]] bool valid() const { return base.valid() && peerId != 0; }

    BaseTransportDependencies
    withBaseDeps(void *ctx, SendCallback sendCallback = nullptr,
                 ReceiveCallback receiveCallback = nullptr,
                 detail::TransportForwardingPolicy forwardingPolicy =
                     detail::TransportForwardingPolicy::SharedBusEdge) {
        if (this->base.transport == nullptr) {
            this->base.transport = ctx;
        }
        if (this->base.sendCallback == nullptr) {
            this->base.sendCallback = sendCallback;
        }
        if (this->base.receiveCallback == nullptr) {
            this->base.receiveCallback = receiveCallback;
        }
        this->base.forwardingPolicy = forwardingPolicy;
        return this->base;
    }
};

struct LocalSharedBusEdgeEgressByteArenaConfig {
    static constexpr size_t bufferSize = 512;
    static constexpr size_t slotCount = 32;
    static constexpr size_t spanCount = 32;
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
 * Common local test transport for shared-bus edge semantics.
 *
 * The transport retains serialized frames in transport-owned storage until a
 * router transport polls them. Frames received from the router are published
 * locally through the normal BaseTransport receive path.
 */
class LocalSharedBusEdgeTransport : public BaseTransport {
    using Base = BaseTransport;

  public:
    explicit LocalSharedBusEdgeTransport(
        LocalSharedBusEdgeTransportDependencies deps)
        : Base(_makeBaseDeps(this, deps)),
          _peerId(deps.peerId),
          _readyAfterMs(deps.readyAfterMs) {
        ABORT_IF_NOT(deps.valid(),
                     "Invalid LocalSharedBusEdgeTransport dependencies");
    }

    DELETE_COPY(LocalSharedBusEdgeTransport)
    DELETE_MOVE(LocalSharedBusEdgeTransport)

    [[nodiscard]] PeerId peerId() const { return _peerId; }

    /**
     * Report whether this edge transport still retains a frame in its
     * transport-owned egress buffer.
     */
    [[nodiscard]] bool hasBufferedFrame(const Header &header) const {
        return _egressBuffer.contains(header);
    }

    [[nodiscard]] bool wasFrameFreed(const Header &header) const {
        return _egressBuffer.wasFreed(header);
    }

    ReturnCode addLink(LocalSharedBusLink &sharedBusLink) {
        FAIL_IF_INACTIVE_ERR("Cannot link inactive shared-bus edge transport");
        FAIL_IF_NOT_NULL(_link, ERR(InvalidState),
                         "Link already established for shared-bus edge");
        FAIL_IF_ERR_FWD(sharedBusLink.begin(),
                        "Failed to begin shared-bus link " SV_FMT,
                        SV_ARG(sharedBusLink.name()));
        _link = &sharedBusLink;
        _log_i("%s attached to %s", name, sharedBusLink.className);
        return OK();
    }

    void setRouterWake(void *routerNode, WakeCallback routerWakeCallback) {
        _routerNode = routerNode;
        _routerWakeCallback = routerWakeCallback;
    }

    ReturnCode notifySharedBusMembershipChanged() {
        if (!available() || _availabilityObserver == nullptr) {
            return OK();
        }
        FAIL_IF_ERR_FWD(
            _availabilityObserver->onTransportAvailabilityChanged(_transportId,
                                                                  true),
            "Failed to notify shared-bus edge membership change");
        return _wake();
    }

    ReturnCode receiveFromRouter(std::span<const std::byte> frame) {
        FAIL_IF_NULL(_link, ERR(InvalidState),
                     "No shared-bus link established for router delivery");
        FAIL_IF_ERR_FWD(_link->sendToEdge(frame),
                        "Failed to enqueue router-delivered frame into "
                        "shared-bus link");
        FAIL_IF_ERR_FWD(_wake(), "Failed to wake shared-bus edge receiver");
        return OK();
    }

    std::expected<size_t, ReturnCode>
    takeFrameForRouter(std::span<std::byte> out) {
        FAIL_IF_NULL(_link, std::unexpected(ERR(InvalidState)),
                     "No shared-bus link established for router poll");
        return _link->receiveForRouter(out);
    }

    ReturnCode enqueueRaw(const Header &header, std::span<const std::byte> frame,
                          const detail::TransportDispatch &dispatch = {})
        override {
        (void)dispatch;
        FAIL_IF_NULL(_link, ERR(InvalidState),
                     "No shared-bus link established for outbound frame");
        FAIL_IF_UNEXPECTED_FWD(storeRet, _egressBuffer.store(header, frame),
                               "Failed to store direct outbound shared-bus "
                               "frame");
        if (!storeRet) {
            return ERR(Timeout);
        }
        auto linkRet = _link->sendToRouter(frame);
        if (!linkRet.ok()) {
            FAIL_IF_ERR_FWD(_egressBuffer.release(header),
                            "Failed to roll back direct outbound "
                            "shared-bus frame after link enqueue failure");
            FAIL(linkRet,
                 "Failed to enqueue direct outbound shared-bus frame into "
                 "link: " ERR_FMT,
                 ERR_ARG(linkRet));
        }
        FAIL_IF_ERR_FWD(_wakeRouter(),
                        "Failed to wake shared-bus router receiver");
        FAIL_IF_ERR_FWD(_egressBuffer.release(header),
                        "Failed to release direct outbound shared-bus frame "
                        "after link handoff");
        return OK();
    }

  protected:
    static BaseTransportDependencies
    _makeBaseDeps(LocalSharedBusEdgeTransport *self,
                  LocalSharedBusEdgeTransportDependencies &deps) {
        auto baseDeps = deps.withBaseDeps(self, _sendCallback, _receiveCallback);
        baseDeps.availableCallback = _availableCallback;
        return baseDeps;
    }

    static bool _availableCallback(void *transport) {
        auto *self = static_cast<LocalSharedBusEdgeTransport *>(transport);
        return self->_available();
    }

    static ReturnCode _sendCallback(void *transport, const Header &header,
                                    std::span<const std::byte> frame) {
        auto *self = static_cast<LocalSharedBusEdgeTransport *>(transport);
        return self->_send(header, frame);
    }

    ReturnCode _send(const Header &header, std::span<const std::byte> frame) {
        FAIL_IF_NULL(_link, ERR(InvalidState),
                     "No shared-bus link established for outbound frame");
        FAIL_IF_UNEXPECTED_FWD(storeRet, _egressBuffer.store(header, frame),
                               "Failed to store outbound shared-bus frame");
        if (!storeRet) {
            _log_w("%s: dropped noncritical outbound shared-bus frame with "
                   "message ID %lu under egress pressure",
                   name, static_cast<unsigned long>(header.messageId));
            return OK();
        }
        auto linkRet = _link->sendToRouter(frame);
        if (!linkRet.ok()) {
            FAIL_IF_ERR_FWD(_egressBuffer.release(header),
                            "Failed to roll back outbound shared-bus frame"
                            " after link enqueue failure");
            FAIL(linkRet,
                 "Failed to enqueue outbound shared-bus frame into link: "
                 ERR_FMT,
                 ERR_ARG(linkRet));
        }
        FAIL_IF_ERR_FWD(_wakeRouter(),
                        "Failed to wake shared-bus router receiver");
        FAIL_IF_ERR_FWD(_egressBuffer.release(header),
                        "Failed to release outbound shared-bus frame after "
                        "link handoff");
        return OK();
    }

    static std::expected<size_t, ReturnCode>
    _receiveCallback(void *transport, std::span<std::byte> out) {
        auto *self = static_cast<LocalSharedBusEdgeTransport *>(transport);
        return self->_receive(out);
    }

    std::expected<size_t, ReturnCode> _receive(std::span<std::byte> out) {
        FAIL_IF_NULL(_link, std::unexpected(ERR(InvalidState)),
                     "No shared-bus link established for inbound frame");
        return _link->receiveForEdge(out);
    }

    [[nodiscard]] bool _available() const {
        if (!_readinessStarted) {
            _readinessStarted = true;
            _readyAtMs = ::platform::get_time() + _readyAfterMs;
        }
        return ::platform::get_time() >= _readyAtMs;
    }

    ReturnCode _wakeRouter() {
        if (_routerWakeCallback == nullptr) {
            return OK();
        }
        return _routerWakeCallback(_routerNode, Signal::Ping);
    }

    PeerId _peerId;
    LocalSharedBusLink *_link = nullptr;
    void *_routerNode = nullptr;
    WakeCallback _routerWakeCallback = nullptr;
    uint32_t _readyAfterMs = 0;
    mutable bool _readinessStarted = false;
    mutable uint32_t _readyAtMs = 0;
    detail::EgressBuffer<LocalSharedBusEdgeEgressByteArenaConfig> _egressBuffer;
};

class LocalDMABufferedTransport : public LocalSharedBusEdgeTransport {
  public:
    explicit LocalDMABufferedTransport(
        LocalSharedBusEdgeTransportDependencies deps)
        : LocalSharedBusEdgeTransport(deps) {}

    static constexpr const char *name = "LocalDMABufferedTransport";
};

class LocalActiveBufferedTransport : public LocalSharedBusEdgeTransport {
  public:
    explicit LocalActiveBufferedTransport(
        LocalSharedBusEdgeTransportDependencies deps)
        : LocalSharedBusEdgeTransport(deps) {}

    static constexpr const char *name = "LocalActiveBufferedTransport";
};

} // namespace Totem::PubSubBackend::Transports
