#pragma once

#include "Data.hh"
#include "Macros/Facade.hh"
#include "PubSubBackend/Facade.hh" // IWYU pragma: export
#include "PubSubBackend/Interfaces/Envelope.hh"
#include "PubSubBackend/Interfaces/Subscriber.hh"
#include "PubSubBackend/Interfaces/Types.hh"
#include "Types/Error.hh"
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
