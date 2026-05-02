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

    static INode &get() {
        ABORT_IF_NULL(backend, "PubSub backend node is not set");
        return *backend;
    }

    static Totem::PubSubBackend::MessageId nextMessageId() {
        ABORT_IF_NULL(backend,
                      "PubSub backend node is not set for nextMessageId");
        return get().nextMessageId();
    }
};
