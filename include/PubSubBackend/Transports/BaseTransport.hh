#pragma once

#include "Base/HasLifecycle.hh"
#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Frame.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Queue/Facade.hh"
#include "Types/Error.hh"
#include <string_view>

namespace Totem::PubSubBackend::Transports {

using SendAckCallback = ReturnCode (*)(void *owner,
                                       detail::TransportId transportId,
                                       const PublishRequest &req);

struct BaseTransportDependencies {
    void *owner;
    detail::TransportId transportId;
    std::string_view name;
    SendAckCallback sendAckCallback;

    [[nodiscard]] bool validate() const {
        return owner != nullptr && sendAckCallback != nullptr;
    }
};

class BaseTransport : public HasLifecycle<BaseTransport> {
    friend class HasLifecycle<BaseTransport>;
    friend struct LifecycleContract<BaseTransport>;

  public:
    explicit BaseTransport(const BaseTransportDependencies &deps)
        : _owner(deps.owner), _instanceName(deps.name),
          _transportId(deps.transportId),
          _sendAckCallback(deps.sendAckCallback) {
        ABORT_IF_NOT(deps.validate(), "Invalid BaseTransport dependencies");
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
        FAIL_IF_ERR_FWD(Totem::Queue::Platform::send(
                            _sendQueue, static_cast<void *>(&frameHandle)),
                        "Failed to enqueue frame for sending");
        return OK();
    }

    ReturnCode work() {
        auto ret = OK();
        detail::FrameHandle item;
        while (ret.ok()) {
            ret.combine(Totem::Queue::Platform::receive(
                _sendQueue, static_cast<void *>(&item), 0));
            if (ret.ok()) {
                FAIL_IF_ERR_FWD(_ack(item->request),
                                "Failed to acknowledge frame with transport");
            }
        }
        if (!ret.ok()) {
            if (ret == ERR(Timeout)) {
                return OK();
            }
            FAIL(ret, "Failed to receive frame from send queue: %s",
                 ret.format());
        }
        return OK();
    }

  protected:
    ReturnCode _ack(const PublishRequest &req) {
        return _sendAckCallback(_owner, _transportId, req);
    }

    ReturnCode _onBegin() {
        auto sendQueueResult =
            Totem::Queue::Platform::create(_sendQueueStorage);
        FAIL_IF_ASSIGN_UNEXPECTED_FWD(_sendQueue, sendQueueResult,
                                      "Failed to create publish queue: %s",
                                      sendQueueResult.error().format());
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        if (_sendQueue != nullptr) {
            FAIL_IF_ERR_FWD(Totem::Queue::Platform::destroy(_sendQueue),
                            "Failed to destroy publish queue");
            _sendQueue = {};
        }
        return ret;
    }

    void *_owner = nullptr;
    std::string_view _instanceName;
    detail::TransportId _transportId;
    SendAckCallback _sendAckCallback = nullptr;
    Totem::Queue::Handle _sendQueue{};
    Totem::Queue::Platform::Storage<detail::FrameHandle,
                                    detail::Spec::Limits::maxMessageQueueSize>
        _sendQueueStorage{};

    using DefaultError = CoreError;

}; // namespace Totem::PubSub::detail

inline constexpr LifecycleContract<BaseTransport>
    _base_transport_lifecycle_contract;

} // namespace Totem::PubSubBackend::Transports
