#pragma once

#include "Data.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Facade.hpp" // IWYU pragma: export
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Subscriber.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include <cstring>
#include <expected>

class PubSubService {
    using Node = Totem::PubSubBackend::Node;

    inline static Node *backend = nullptr;

  public:
    using Topic = NodeData::PubSub::Topic;

    static void setBackend(Node &backendNode) { backend = &backendNode; }

    static Node &node() {
        ABORT_IF_NULL(backend, "PubSub backend node is not set");
        return *backend;
    }

    static std::expected<Node::SubscriberKey, ReturnCode>
    subscribe(const char *subscriberName,
              const Totem::PubSubBackend::Subscriber &subscriber, Topic topic) {
        return node().subscribe(subscriberName, subscriber, topic);
    }

    static ReturnCode unsubscribe(const Node::SubscriberKey &subscriberKey) {
        return node().unsubscribe(subscriberKey);
    }

    static ReturnCode publish(const Totem::PubSubBackend::Envelope &envelope) {
        return node().publish(envelope);
    }

    static ReturnCode ack(NodeData::PubSub::Transport transport,
                          const Totem::PubSubBackend::Envelope &envelope) {
        return Node::ack(backend, transport, envelope);
    }

    [[nodiscard]] static Totem::PubSubBackend::MessageId nextMessageId() {
        return node().nextMessageId();
    }
};
