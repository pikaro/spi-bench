#pragma once

#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Wire.hpp"
#include "PubSubBackend/Transports/BaseTransport.hpp"
#include "PubSubBackend/detail/SerDe.hpp"
#include "PubSubBackend/detail/Trace.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Queue/Facade.hpp"
#include "Types/Error.hpp"
#include "Wire/Interfaces/Request.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <span>

namespace Totem::PubSubBackend::Transports {

template <class Link> class Rs485Transport;

template <class Link> struct Rs485TransportDependencies {
    BaseTransportDependencies base;
    Link &link;

    [[nodiscard]] bool valid() const { return base.valid(); }

    BaseTransportDependencies
    withBaseDeps(void *ctx, SendCallback sendCallback = nullptr,
                 ReceiveCallback receiveCallback = nullptr) {
        if (this->base.transport == nullptr) {
            this->base.transport = ctx;
        }
        if (this->base.sendCallback == nullptr) {
            this->base.sendCallback = sendCallback;
        }
        if (this->base.receiveCallback == nullptr) {
            this->base.receiveCallback = receiveCallback;
        }
        return this->base;
    }
};

template <class Link> class Rs485Transport : public BaseTransport {
    using Base = BaseTransport;

    static constexpr size_t rxQueueDepth = 4;
    static constexpr size_t inFlightDepth = 4;

    struct RxFrame {
        std::array<std::byte, Base::bufferSize> data{};
        size_t size = 0;
    };

    struct InFlightFrame {
        Rs485Transport *transport = nullptr;
        Envelope envelope{};
        Header header{};
        std::array<std::byte, Base::bufferSize> data{};
        size_t size = 0;
        bool occupied = false;
    };

  public:
    explicit Rs485Transport(Rs485TransportDependencies<Link> deps)
        : Base(_makeBaseDeps(this, deps)), _link(deps.link) {
        ABORT_IF_NOT(deps.valid(), "Invalid Rs485Transport dependencies");
    }

    DELETE_COPY(Rs485Transport)
    DELETE_MOVE(Rs485Transport)

    static constexpr const char *name = "Rs485Transport";

    ReturnCode registerHandler() {
        return _link.registerHandler(Wire::FrameHandler{
            .owner = this,
            .payloadType = Wire::PayloadType::PubSub,
            .response = {},
            .onData = _onWireData,
            .onRequest = nullptr,
        });
    }

    [[nodiscard]] bool wasFrameFreed(const Header &header) const {
        return _findInFlight(header) == nullptr;
    }

    ReturnCode
    send(size_t maxCount = std::numeric_limits<size_t>::max()) override {
        FAIL_IF_INACTIVE_ERR("Cannot work inactive transport %s",
                             _instanceName);
        FAIL_IF_ERR_FWD(_observeAvailability(),
                        "Failed to observe RS485 transport availability");
        if (!_available()) {
            return OK();
        }

        size_t count = 0;
        while (count < maxCount) {
            auto *slot = _findFreeInFlight();
            if (slot == nullptr) {
                return OK();
            }

            detail::FrameHandle item{};
            auto receiveRet = Totem::Queue::Platform::receive(
                _sendQueue, static_cast<void *>(&item), 0);
            if (!receiveRet.ok()) {
                if (receiveRet == ERR(Timeout)) {
                    return OK();
                }
                FAIL(receiveRet,
                     "Failed to receive frame from RS485 transport "
                     "queue: " ERR_FMT,
                     ERR_ARG(receiveRet));
            }

            auto frameSizeResult = _prepareFrameForSend(item, slot->data);
            if (!frameSizeResult) {
                _log_w(SV_FMT
                       ": dropping unserializable PubSub message %u: " ERR_FMT,
                       SV_ARG(_instanceName), item->envelope.header.messageId,
                       ERR_ARG(frameSizeResult.error()));
                auto ackRet = _ack(item->envelope);
                if (!ackRet.ok()) {
                    _log_w(SV_FMT
                           ": dropped PubSub message %u was not tracked for "
                           "release: " ERR_FMT,
                           SV_ARG(_instanceName),
                           item->envelope.header.messageId, ERR_ARG(ackRet));
                }
                ++count;
                continue;
            }
            auto frameSize = *frameSizeResult;
            detail::log_trace_packet("rs485.tx.prepared", item->envelope.header,
                                     _instanceName.data());
            slot->envelope = item->envelope;
            slot->header = item->envelope.header;
            slot->size = frameSize;
            slot->transport = this;
            slot->occupied = true;

            auto request = Wire::WriteRequest{
                .owner = slot,
                .payloadType = Wire::PayloadType::PubSub,
                .data =
                    std::span<const std::byte>{slot->data.data(), slot->size},
                .onComplete = _onWireWriteComplete,
            };
            auto sendRet = _link.send(request);
            if (!sendRet.ok()) {
                slot->occupied = false;
                FAIL(sendRet,
                     "Failed to enqueue PubSub frame on RS485 link: " ERR_FMT,
                     ERR_ARG(sendRet));
            }
            detail::log_trace_packet("rs485.tx.queued", item->envelope.header,
                                     _instanceName.data());
            ++count;
        }
        return OK();
    }

  private:
    static BaseTransportDependencies
    _makeBaseDeps(Rs485Transport *self,
                  Rs485TransportDependencies<Link> &deps) {
        auto baseDeps =
            deps.withBaseDeps(self, _sendCallback, _receiveCallback);
        baseDeps.availableCallback = _availableCallback;
        return baseDeps;
    }

    static bool _availableCallback(void *transport) {
        auto *self = static_cast<Rs485Transport *>(transport);
        return self->_link.ready();
    }

    static ReturnCode _sendCallback(void * /*transport*/,
                                    const Header & /*header*/,
                                    std::span<const std::byte> /*frame*/) {
        return ERR(CoreError, InvalidState);
    }

    static std::expected<size_t, ReturnCode>
    _receiveCallback(void *transport, std::span<std::byte> out) {
        auto *self = static_cast<Rs485Transport *>(transport);
        return self->_receive(out);
    }

    std::expected<size_t, ReturnCode> _receive(std::span<std::byte> out) {
        FAIL_IF_ERR_FWD_UNEXPECTED(_ensureRxFrameQueue(),
                                   "Failed to ensure RS485 RX queue");
        RxFrame rxFrame{};
        auto receiveRet =
            Totem::Queue::Platform::receive(_rxFrameQueue, &rxFrame, 0);
        if (!receiveRet.ok()) {
            if (receiveRet == ERR(Timeout)) {
                return std::unexpected(ERR(Timeout));
            }
            FAIL(std::unexpected(receiveRet),
                 "Failed to receive frame from RS485 RX queue: " ERR_FMT,
                 ERR_ARG(receiveRet));
        }
        FAIL_IF(out.size() < rxFrame.size,
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "Output buffer too small for RS485 PubSub frame");
        std::memcpy(out.data(), rxFrame.data.data(), rxFrame.size);
        detail::log_trace_frame(
            "rs485.rx.dequeue",
            std::span<const std::byte>{out.data(), rxFrame.size},
            detail::SerDe::peekHeader, _instanceName.data());
        return rxFrame.size;
    }

    static ReturnCode _onWireData(void *owner,
                                  Wire::PayloadType /*payloadType*/,
                                  std::span<const std::byte> payload,
                                  int64_t /*receivedAtUs*/) {
        auto *self = static_cast<Rs485Transport *>(owner);
        return self->_receiveWireData(payload);
    }

    ReturnCode _receiveWireData(std::span<const std::byte> payload) {
        FAIL_IF(payload.size() > Base::bufferSize, ERR(CoreError, Overflow),
                "RS485 PubSub frame exceeds transport buffer size");
        FAIL_IF_ERR_FWD(_ensureRxFrameQueue(),
                        "Failed to ensure RS485 RX queue for ingress");

        RxFrame rxFrame{};
        std::memcpy(rxFrame.data.data(), payload.data(), payload.size());
        rxFrame.size = payload.size();
        detail::log_trace_frame("rs485.wire.ingress", payload,
                                detail::SerDe::peekHeader,
                                _instanceName.data());
        FAIL_IF_ERR_FWD(Totem::Queue::Platform::send(_rxFrameQueue, &rxFrame,
                                                     queueSendTimeoutTicks),
                        "Failed to enqueue RS485 PubSub ingress frame");
        FAIL_IF_ERR_FWD(_wake(), "Failed to wake PubSub after RS485 ingress");
        return OK();
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
        if (!slot.occupied) {
            return OK();
        }

        auto envelope = slot.envelope;
        detail::log_trace_packet("rs485.tx.complete", envelope.header,
                                 _instanceName.data());
        slot = {};

        if (!result.result.ok()) {
            _log_w(SV_FMT
                   ": RS485 write failed for PubSub message %u: " ERR_FMT,
                   SV_ARG(_instanceName), envelope.header.messageId,
                   ERR_ARG(result.result));
        }
        return _ack(envelope);
    }

    ReturnCode _ensureRxFrameQueue() {
        if (_rxFrameQueue != nullptr) {
            return OK();
        }
        auto queueResult = Totem::Queue::Platform::create(_rxFrameQueueStorage);
        FAIL_IF_UNEXPECTED_FWD(queueHandle, queueResult,
                               "Failed to create RS485 RX queue");
        _rxFrameQueue = queueHandle;
        return OK();
    }

    InFlightFrame *_findFreeInFlight() {
        for (auto &slot : _inFlight) {
            if (!slot.occupied) {
                return &slot;
            }
        }
        return nullptr;
    }

    const InFlightFrame *_findInFlight(const Header &header) const {
        for (const auto &slot : _inFlight) {
            if (slot.occupied && slot.header == header) {
                return &slot;
            }
        }
        return nullptr;
    }

    Link &_link;

    Totem::Queue::Handle _rxFrameQueue{};
    Totem::Queue::Platform::Storage<RxFrame, rxQueueDepth>
        _rxFrameQueueStorage{};

    std::array<InFlightFrame, inFlightDepth> _inFlight{};

    static constexpr ::platform::Tick queueSendTimeoutTicks = 1;
};

} // namespace Totem::PubSubBackend::Transports
