#pragma once

#include "Data.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Subscriber.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"

// IWYU pragma: begin_exports

#include "PubSubBackend/detail/Pool.hpp"
#include "PubSubBackend/detail/Types.hpp"

// IWYU pragma: end_exports

#include "Types/Error.hpp"
#include <cstddef>
#include <cstring>
#include <expected>

namespace Totem::PubSubBackend::detail {

struct INode {
    virtual ~INode() = default;

    virtual std::expected<SubscriberKey, ReturnCode>
    subscribe(const char *subscriberName, const Subscriber &subscriber,
              NodeData::PubSub::Topic topic) = 0;
    virtual ReturnCode unsubscribe(const SubscriberKey &subscriberKey) = 0;
    virtual ReturnCode publish(const Envelope &envelope) = 0;
    virtual ReturnCode ack(NodeData::PubSub::Transport transport,
                           const Envelope &envelope) = 0;
    [[nodiscard]] virtual NodeData::PubSub::NodeId nodeId() const = 0;
    [[nodiscard]] virtual MessageId nextMessageId() = 0;
};

} // namespace Totem::PubSubBackend::detail

namespace Totem::PubSubBackend {

using detail::Pool;

} // namespace Totem::PubSubBackend

class PubSubService {
    using INode = Totem::PubSubBackend::detail::INode;
    inline static INode *backend = nullptr;

  public:
    using Topic = NodeData::PubSub::Topic;

    static void set(INode &backendNode) { backend = &backendNode; }

    [[nodiscard]] static bool configured() { return backend != nullptr; }

    static INode &get() {
        ABORT_IF_NULL(backend, "PubSub backend node is not set");
        return *backend;
    }

    static Totem::PubSubBackend::MessageId nextMessageId() {
        ABORT_IF_NULL(backend,
                      "PubSub backend node is not set for nextMessageId");
        return get().nextMessageId();
    }

    /** Publish a task-context value through a bounded, type-specific pool. */
    template <size_t PoolSize = 8, typename Event>
    static ReturnCode publish(Topic topic, const Event &event,
                              bool requireSyncedClock = false) {
        using Pool = Totem::PubSubBackend::Pool<Event, PoolSize>;
        static Pool pool{};

        FAIL_IF_NOT(configured(), ERR(CoreError, InvalidState),
                    "PubSub backend is not configured");

        auto &pubSub = get();
        const auto messageId = pubSub.nextMessageId();
        FAIL_IF(messageId == 0, ERR(CoreError, InvalidState),
                "PubSub returned message ID 0");

        auto stored = pool.store(event, messageId);
        if (!stored) {
            FAIL_ERR_FWD(stored.error(), "Failed to store PubSub event");
        }

        auto envelopeResult = Totem::PubSubBackend::Envelope::make<Event>({
            .owner = static_cast<void *>(&pool),
            .topic = topic,
            .messageId = messageId,
            .getPayloadPtr = Pool::getPtr,
            .encodePayload = Pool::encodePayload,
            .release = Pool::release,
            .requireSyncedClock = requireSyncedClock,
        });
        if (!envelopeResult) {
            REPORT_IF_ERR(pool.release({.header = {.messageId = messageId}}),
                          "Failed to release event after envelope failure");
            FAIL_ERR_FWD(envelopeResult.error(),
                         "Failed to create PubSub event envelope");
        }

        auto publishRet = pubSub.publish(*envelopeResult);
        if (!publishRet.ok()) {
            REPORT_IF_ERR(pool.release(*envelopeResult),
                          "Failed to release event after publish failure");
            FAIL_ERR_FWD(publishRet, "Failed to publish PubSub event");
        }
        return OK();
    }
};
