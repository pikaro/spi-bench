#pragma once

#include "BaseTransport.hh"
#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Wire.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Queue/Facade.hh"
#include "Types/Error.hh"
#include <array>
#include <cstddef>
#include <cstring>
#include <expected>
#include <span>

namespace Totem::PubSubBackend::Transports {

struct LocalTransportDependencies {
    BaseTransportDependencies base;

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

class LocalTransport : public BaseTransport {
    using Base = BaseTransport;
    friend struct BaseTransportContract;

    struct RxFrame {
        std::array<std::byte, Base::bufferSize> data;
        size_t size;
    };

  public:
    explicit LocalTransport(LocalTransportDependencies deps)
        : Base(deps.withBaseDeps(this, _sendCallback, _receiveCallback)) {
        ABORT_IF_NOT(deps.valid(), "Invalid LocalTransport dependencies");
    }

    DELETE_COPY(LocalTransport)
    DELETE_MOVE(LocalTransport)

    static constexpr const char *name = "LocalTransport";

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
        return OK();
    }

  protected:
    static ReturnCode _sendCallback(void *localTransport, const Header &header,
                                    std::span<const std::byte> frame) {
        auto *self = static_cast<LocalTransport *>(localTransport);
        return self->_send(header, frame);
    }

    ReturnCode _send(const Header & /*unused*/,
                     std::span<const std::byte> frame) {
        FAIL_IF_NULL(link, ERR(InvalidState),
                     "No link established for LocalTransport");
        FAIL_IF_ERR_FWD(link->_receiveThroughLink(frame),
                        "Failed to send frame over LocalTransport");
        return {};
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
        FAIL_IF_ERR_FWD(
            Totem::Queue::Platform::send(_rxFrameQueue, &rxFrame, 0),
            "Failed to send frame to rxFrame queue");
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
        return OK();
    }

    LocalTransport *link;

    Totem::Queue::Handle _rxFrameQueue{};
    Totem::Queue::Platform::Storage<RxFrame,
                                    detail::Spec::Limits::maxMessageQueueSize>
        _rxFrameQueueStorage{};
    using DefaultError = CoreError;
};

} // namespace Totem::PubSubBackend::Transports
