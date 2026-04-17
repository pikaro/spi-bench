#pragma once

#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/detail/IngressBuffer.hpp"
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

struct BaseTransportDependencies {
    void *pubSubNode;
    void *transport = nullptr;
    detail::TransportId transportId;
    std::string_view name;
    SendAckCallback sendAckCallback;
    SendCallback sendCallback = nullptr;
    ReceiveCallback receiveCallback = nullptr;
    detail::IngressBuffer &ingress;

    [[nodiscard]] bool valid() const {
        return pubSubNode != nullptr && transport != nullptr &&
               sendAckCallback != nullptr && receiveCallback != nullptr &&
               sendCallback != nullptr && !name.empty();
    }
};

class BaseTransport : public HasLifecycle<BaseTransport> {
    friend class HasLifecycle<BaseTransport>;
    friend struct LifecycleContract<BaseTransport>;
    using Topic = typename detail::Spec::Topic;
    using NodeId = typename detail::Spec::NodeId;

  public:
    explicit BaseTransport(const BaseTransportDependencies &deps)
        : _pubSubNode(deps.pubSubNode), _transport(deps.transport),
          _instanceName(deps.name), _transportId(deps.transportId),
          _sendAckCallback(deps.sendAckCallback),
          _sendCallback(deps.sendCallback),
          _receiveCallback(deps.receiveCallback), _ingress(deps.ingress) {
        ABORT_IF_NOT(deps.valid(), "Invalid BaseTransport dependencies");
    }

    DELETE_COPY(BaseTransport)
    DELETE_MOVE(BaseTransport)

    static constexpr const char *name = "BaseTransport";

    [[nodiscard]] detail::TransportId transportId() const {
        return _transportId;
    }
    [[nodiscard]] std::string_view instanceName() const {
        return _instanceName;
    }

    ReturnCode enqueue(detail::FrameHandle frameHandle) {
        _log_d(SV_FMT ": enqueue send for " MAGIC_PUBSUB_SV_FMT,
               SV_ARG(_instanceName),
               MAGIC_PUBSUB_SV_ARG(frameHandle->envelope.header));
        FAIL_IF_ERR_FWD(Totem::Queue::Platform::send(
                            _sendQueue, static_cast<void *>(&frameHandle)),
                        "Failed to enqueue frame for sending");
        return OK();
    }

    ReturnCode send(size_t maxCount = std::numeric_limits<size_t>::max()) {
        FAIL_IF_INACTIVE_ERR("Cannot work inactive transport %s",
                             _instanceName);
        auto ret = OK();
        size_t count = 0;

        detail::FrameHandle item;
        auto buf = std::array<std::byte, bufferSize>{};

        while (ret.ok() && count < maxCount) {
            ret.combine(Totem::Queue::Platform::receive(
                _sendQueue, static_cast<void *>(&item), 0));
            if (ret.ok()) {
                _log_d(SV_FMT ": dequeued send for " MAGIC_PUBSUB_SV_FMT,
                       SV_ARG(_instanceName),
                       MAGIC_PUBSUB_SV_ARG(item->envelope.header));
                FAIL_IF_UNEXPECTED_FWD(
                    frameSize, detail::SerDe::serialize(item->envelope, buf),
                    "Failed to serialize frame for sending");
                auto frame = std::span<const std::byte>{buf.data(), frameSize};
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

    ReturnCode receive(size_t maxCount = std::numeric_limits<size_t>::max()) {
        FAIL_IF_INACTIVE_ERR("Cannot work inactive transport %s",
                             _instanceName);
        auto ret = OK();
        size_t count = 0;

        auto buf = std::array<std::byte, bufferSize>{};

        while (ret.ok() && count < maxCount) {
            auto receiveResult = _receiveCallback(_transport, buf);

            if (receiveResult) {
                _log_d(SV_FMT ": received raw frame of %zu bytes",
                       SV_ARG(_instanceName), *receiveResult);
                FAIL_IF_UNEXPECTED_FWD(
                    envelope,
                    _ingress.storeFrame(
                        std::span<const std::byte>{buf.data(), *receiveResult}),
                    "Failed to store received frame in ingress");
                _log_d(SV_FMT
                       ": enqueuing received envelope for " MAGIC_PUBSUB_SV_FMT,
                       SV_ARG(_instanceName),
                       MAGIC_PUBSUB_SV_ARG(envelope.header));
                FAIL_IF_ERR_FWD(
                    Totem::Queue::Platform::send(
                        _publishQueue, static_cast<void *>(&envelope)),
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

    ReturnCode pollInto(void *ctx, detail::PollIntoCallback callback,
                        size_t maxCount = std::numeric_limits<size_t>::max()) {
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
            FAIL_IF_ERR_FWD(callback(ctx, item),
                            "Failed to process item from publish queue");
        }
        std::unreachable();
    }

  protected:
    ReturnCode _ack(const Envelope &envelope) {
        _log_d(SV_FMT ": ack " MAGIC_PUBSUB_SV_FMT, SV_ARG(_instanceName),
               MAGIC_PUBSUB_SV_ARG(envelope.header));
        return _sendAckCallback(_pubSubNode, _transportId, envelope);
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
    SendAckCallback _sendAckCallback = nullptr;
    SendCallback _sendCallback = nullptr;
    ReceiveCallback _receiveCallback = nullptr;

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
