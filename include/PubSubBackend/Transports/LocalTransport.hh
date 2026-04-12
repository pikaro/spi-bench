#pragma once

#include "BaseTransport.hh"
#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Frame.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Queue/Facade.hh"
#include "Types/Error.hh"
#include <expected>

namespace Totem::PubSubBackend::Transports {

struct LocalTransportDependencies {
    BaseTransportDependencies base;

    [[nodiscard]] bool validate() const { return base.validate(); }
};

class LocalTransport : public BaseTransport {
    using Base = BaseTransport;

  public:
    explicit LocalTransport(LocalTransportDependencies deps) : Base(deps.base) {
        ABORT_IF_NOT(deps.validate(), "Invalid LocalTransport dependencies");
    }

    ReturnCode pollInto(void *ctx, detail::PollIntoCallback callback) {
        auto ret = OK();
        while (ret.ok()) {
            auto itemResult = _getItemFromLocalQueue();
            if (!itemResult) {
                if (itemResult.error() == ERR(Timeout)) {
                    return OK();
                }
                FAIL(itemResult.error(),
                     "Failed to receive item from local queue: %s",
                     itemResult.error().format());
            }
            ret.combine(callback(ctx, *itemResult));
        }
        return ret;
    }

    ReturnCode send(const PublishRequest &request) {
        return Totem::Queue::Platform::send(_localQueue, &request);
    }

  private:
    ReturnCode _onBegin() {
        auto sendQueueResult =
            Totem::Queue::Platform::create(_localQueueStorage);
        FAIL_IF_ASSIGN_UNEXPECTED_FWD(_localQueue, sendQueueResult,
                                      "Failed to create publish queue: %s",
                                      sendQueueResult.error().format());
        return Base::_onBegin();
    }

    std::expected<PublishRequest, ReturnCode> _getItemFromLocalQueue() {
        PublishRequest item;
        auto result = Totem::Queue::Platform::receive(_localQueue, &item);
        if (!result.ok()) {
            return std::unexpected(result);
        }
        return item;
    }

    Totem::Queue::Handle _localQueue{};
    Totem::Queue::Platform::Storage<PublishRequest,
                                    detail::Spec::Limits::maxMessageQueueSize>
        _localQueueStorage{};

    using DefaultError = CoreError;
};

} // namespace Totem::PubSubBackend::Transports
