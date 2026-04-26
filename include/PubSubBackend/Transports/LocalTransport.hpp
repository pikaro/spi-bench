#pragma once

#include "LocalPollingTransport.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Wire.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Queue/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <utility>

namespace Totem::PubSubBackend::Transports {

struct LocalTransportDependencies {
    BaseTransportDependencies base;
    uint32_t readyAfterMs = 0;

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

class LocalTransport : public LocalPollingTransport {
    using Base = LocalPollingTransport;
    friend struct BaseTransportContract;
    // Point-to-point local links only bridge one peer pair. A small RX queue
    // is enough to absorb scheduler jitter without paying for a full
    // maxMessageQueueSize buffer per endpoint.
    static constexpr size_t rxQueueDepth = 4;

    struct RxFrame {
        std::array<std::byte, Base::bufferSize> data;
        size_t size;
    };

  public:
    explicit LocalTransport(LocalTransportDependencies deps)
        : Base(_makeBaseDeps(this, deps)), _readyAfterMs(deps.readyAfterMs) {
        ABORT_IF_NOT(deps.valid(), "Invalid LocalTransport dependencies");
    }

    DELETE_COPY(LocalTransport)
    DELETE_MOVE(LocalTransport)

    static constexpr const char *name = "LocalTransport";

    /**
     * Point-to-point local transport does not retain a transport-owned egress
     * copy after enqueueing onto the linked peer RX queue.
     */
    [[nodiscard]] bool wasFrameFreed(const Header & /*header*/) const {
        return true;
    }

    ReturnCode addLink(LocalTransport &other) {
        FAIL_IF_INACTIVE_ERR("Cannot link inactive LocalTransport");
        FAIL_IF_NOT(other.active(), ERR(InvalidState),
                    "Cannot link to an inactive LocalTransport");
        FAIL_IF_NOT_NULL(link, ERR(InvalidState),
                         "Link already established for LocalTransport");
        FAIL_IF_NOT_NULL(other.link, ERR(InvalidState),
                         "Link already established for other LocalTransport");
        link = &other;
        other.link = this;
        _log_i("%s linked to %s", name, other.name);
        return OK();
    }

    ReturnCode receive(size_t maxCount = std::numeric_limits<size_t>::max())
        override {
        return _receiveAvailabilityOnly(maxCount);
    }

    ReturnCode pollInto(void *ctx, detail::PollIntoCallback callback,
                        size_t maxCount = std::numeric_limits<size_t>::max())
        override {
        return _pollReceiveCallbackInto(
            ctx, callback, detail::IngressContext{.transportId = _transportId},
            maxCount);
    }

  protected:
    static BaseTransportDependencies
    _makeBaseDeps(LocalTransport *self, LocalTransportDependencies &deps) {
        auto baseDeps = deps.withBaseDeps(self, _sendCallback, _receiveCallback);
        baseDeps.availableCallback = _availableCallback;
        return baseDeps;
    }

    static bool _availableCallback(void *transport) {
        auto *self = static_cast<LocalTransport *>(transport);
        return self->_available();
    }

    static ReturnCode _sendCallback(void *localTransport, const Header &header,
                                    std::span<const std::byte> frame) {
        auto *self = static_cast<LocalTransport *>(localTransport);
        return self->_send(header, frame);
    }

    ReturnCode _send(const Header & /*unused*/,
                     std::span<const std::byte> frame) {
        FAIL_IF_NULL(link, ERR(InvalidState),
                     "No link established for LocalTransport");
        _log_d("%s: forwarding %zu-byte frame over local link", name,
               frame.size());
        FAIL_IF_ERR_FWD(link->_receiveThroughLink(frame),
                        "Failed to send frame over LocalTransport");
        return OK();
    }

    static std::expected<size_t, ReturnCode>
    _receiveCallback(void *localTransport, std::span<std::byte> out) {
        auto *self = static_cast<LocalTransport *>(localTransport);
        return self->_receive(out);
    }

    std::expected<size_t, ReturnCode> _receive(std::span<std::byte> out) {
        FAIL_IF_NULL(link, std::unexpected(ERR(InvalidState)),
                     "No link established for LocalTransport");
        FAIL_IF_ERR_FWD_UNEXPECTED(_ensureRxFrameQueue(),
                                   "Failed to ensure rxFrame queue");
        RxFrame rxFrame;
        auto receiveRet =
            Totem::Queue::Platform::receive(_rxFrameQueue, &rxFrame, 0);
        if (!receiveRet.ok()) {
            if (receiveRet == ERR(Timeout)) {
                return std::unexpected(ERR(Timeout));
            }
            FAIL(std::unexpected(receiveRet),
                 "Failed to receive frame from rxFrame queue: " ERR_FMT,
                 ERR_ARG(receiveRet));
        }
        FAIL_IF(out.size() < rxFrame.size,
                std::unexpected(ERR(InvalidArgument)),
                "Output buffer too small for received frame");
        std::memcpy(out.data(), rxFrame.data.data(), rxFrame.size);
        _log_d("%s: dequeued %zu-byte frame from local RX queue", name,
               rxFrame.size);
        return rxFrame.size;
    }

    ReturnCode _receiveThroughLink(std::span<const std::byte> frame) {
        FAIL_IF_ERR_FWD(_ensureRxFrameQueue(),
                        "Failed to ensure rxFrame queue for linked receive");
        RxFrame rxFrame;
        FAIL_IF(frame.size() > rxFrame.data.size(), ERR(InvalidArgument),
                "Frame size exceeds maximum for LocalTransport");
        std::memcpy(rxFrame.data.data(), frame.data(), frame.size());
        rxFrame.size = frame.size();
        _log_d("%s: enqueue %zu-byte frame into local RX queue", name,
               frame.size());
        FAIL_IF_ERR_FWD(
            Totem::Queue::Platform::send(_rxFrameQueue, &rxFrame,
                                         queueSendTimeoutTicks),
            "Failed to send frame to rxFrame queue");
        FAIL_IF_ERR_FWD(_wake(), "Failed to wake LocalTransport peer");
        return OK();
    }

    ReturnCode _ensureRxFrameQueue() {
        if (_rxFrameQueue != nullptr) {
            return OK();
        }
        auto queueResult = Totem::Queue::Platform::create(_rxFrameQueueStorage);
        FAIL_IF_UNEXPECTED_FWD(queueHandle, std::move(queueResult),
                               "Failed to create rxFrame queue");
        _rxFrameQueue = queueHandle;
        _log_i("%s: created RX queue", name);
        return OK();
    }

    [[nodiscard]] bool _available() const {
        return _selfReady() && link != nullptr && link->_selfReady();
    }

    [[nodiscard]] bool _selfReady() const {
        if (!_readinessStarted) {
            _readinessStarted = true;
            _readyAtMs = ::platform::get_time() + _readyAfterMs;
        }
        return ::platform::get_time() >= _readyAtMs;
    }

    LocalTransport *link = nullptr;
    uint32_t _readyAfterMs = 0;
    mutable bool _readinessStarted = false;
    mutable uint32_t _readyAtMs = 0;

    Totem::Queue::Handle _rxFrameQueue{};
    Totem::Queue::Platform::Storage<RxFrame, rxQueueDepth> _rxFrameQueueStorage{};

    static constexpr ::platform::Tick queueSendTimeoutTicks = 1;
};

} // namespace Totem::PubSubBackend::Transports
