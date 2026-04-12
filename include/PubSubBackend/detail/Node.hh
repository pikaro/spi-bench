#pragma once

#include "Base/HasLifecycle.hh"
#include "Base/HasTaskController.hh"
#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Envelope.hh"
#include "PubSubBackend/Interfaces/Subscriber.hh"
#include "PubSubBackend/Interfaces/Types.hh"
#include "PubSubBackend/detail/ControlPlane.hh"
#include "PubSubBackend/detail/Drainer.hh"
#include "PubSubBackend/detail/IngressBuffer.hh"
#include "PubSubBackend/detail/Publisher.hh"
#include "PubSubBackend/detail/SubscriberDirectory.hh"
#include "PubSubBackend/detail/SubscriptionManager.hh"
#include "PubSubBackend/detail/Transporter.hh"
#include "PubSubBackend/detail/TransporterDirectory.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Queue/Facade.hh"
#include "TaskController/Interfaces/RegistryHooks.hh"
#include "TaskController/Interfaces/TaskHooks.hh"
#include "Types/Error.hh"
#include "magic_enum/magic_enum.hpp"
#include <atomic>
#include <climits>
#include <expected>

namespace Totem::PubSubBackend::detail {

class Node : public HasLifecycle<Node>, public HasTaskController<Node> {
    friend class HasLifecycle<Node>;
    friend struct LifecycleContract<Node>;
    friend struct TaskController::TaskHooks::Contract<Node>;

    using TransporterNameKey = TransporterDirectory::EntryNameKey;
    using SubscriberNameKey = SubscriberDirectory::EntryNameKey;

    using Transport = typename Spec::Transport;

  public:
    explicit Node(TaskController::RegistryHooks registryHooks)
        : HasTaskController<Node>(registryHooks) {}

    DELETE_COPY(Node)
    DELETE_MOVE(Node)

    using Topic = typename Spec::Topic;

    static constexpr const char *name = "PubSub::Node";

    template <class T>
        requires requires { sizeof(Transporter::Contract<T>); }
    std::expected<TransporterNameKey, ReturnCode>
    registerTransport(T &transport) {
        auto transporter = Transporter::bind(transport);
        return _transporters.add(transport.transportId(),
                                 transport.instanceName(), transporter);
    }

    ReturnCode deregisterTransport(TransporterNameKey transportNameKey) {
        return _transporters.remove(transportNameKey);
    }

    std::expected<SubscriberNameKey, ReturnCode>
    subscribe(const char *subscriberName,
              const SubscriberCallback &subscriberCallback, Topic topic) {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            subscriberNameKey,
            _subscribers.add(subscriberName, subscriberCallback,
                             static_cast<TopicId>(topic)),
            "Failed to add subscriber %s for topic " SV_FMT, subscriberName,
            SV_ARG(magic_enum::enum_name(topic)));
        FAIL_IF_ERR_FWD_UNEXPECTED(
            _subscriptionManager.registerSubscription(
                static_cast<TopicId>(topic)),
            "Failed to register subscription for topic " SV_FMT,
            SV_ARG(magic_enum::enum_name(topic)));
        return subscriberNameKey;
    }

    ReturnCode unsubscribe(SubscriberNameKey subscriberNameKey) {
        auto topicResult = _subscribers.topicForSubscriber(subscriberNameKey);
        FAIL_IF_UNEXPECTED_FWD(
            topic, topicResult, "Failed to get topic for subscriber %s->%s",
            _subscribers.ownerName(), subscriberNameKey.name.data());
        FAIL_IF_ERR_FWD(
            _subscriptionManager.deregisterSubscription(topic),
            "Failed to deregister subscription for topic " SV_FMT,
            SV_ARG(magic_enum::enum_name(static_cast<Topic>(topic))));
        return _subscribers.remove(subscriberNameKey);
    }

    ReturnCode publish(const Envelope &req) {
        FAIL_IF_ERR_FWD(Totem::Queue::Platform::send(_publishQueue, &req),
                        "Failed to enqueue publish request for topic " SV_FMT,
                        MAGIC_SV_ARG(Topic, req.header.topic));
        return OK();
    }

    static ReturnCode publish(void *opaque, const Envelope &req) {
        auto *node = static_cast<Node *>(opaque);
        return node->publish(req);
    }

    static ReturnCode ack(void *opaque, TransportId transportId,
                          const Envelope &req) {
        auto *node = static_cast<Node *>(opaque);
        return node->_drainer.ack(static_cast<Spec::Transport>(transportId),
                                  req);
    }

    [[nodiscard]] static NodeId nodeId() {
        return static_cast<NodeId>(Spec::nodeId);
    }
    [[nodiscard]] MessageId nextMessageId() {
        return _nextMessageId.fetch_add(1, std::memory_order_relaxed);
    }
    [[nodiscard]] static MessageId nextMessageId(void *opaque) {
        auto *node = static_cast<Node *>(opaque);
        return node->nextMessageId();
    }
    [[nodiscard]] IngressBuffer &ingress() { return _ingress; }

  private:
    ReturnCode _onBegin() {
        auto publishQueueResult =
            Totem::Queue::Platform::create(_publishQueueStorage);
        FAIL_IF_UNEXPECTED_FWD(publishQueue, publishQueueResult,
                               "Failed to create publish queue: %s",
                               publishQueueResult.error().format());
        _publishQueue = publishQueue;
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        if (_publishQueue != nullptr) {
            ret.combine(Totem::Queue::Platform::destroy(_publishQueue));
            _publishQueue = nullptr;
        }
        return ret;
    }

    ReturnCode _onTaskStep() {
        FAIL_IF_ERR_FWD(_drainer.drain(), "Failed to drain PubSub messages");
        FAIL_IF_ERR_FWD(
            _transporters.withAll([](const TransporterNameKey & /*unused*/,
                                     const TransporterEntry &entry) {
                return entry.transporter.send();
            }),
            "Failed to work PubSub transporters");
        return OK();
    }

    std::atomic<MessageId> _nextMessageId{1};

    TransporterDirectory _transporters{name};
    SubscriberDirectory _subscribers{name};

    Totem::Queue::Platform::Storage<Envelope, Spec::Limits::maxMessageQueueSize>
        _publishQueueStorage;
    Totem::Queue::Handle _publishQueue;

    SubscriptionManager _subscriptionManager{{
        .pubSubNode = this,
        .transporters = _transporters,
        .publishCallback = Node::publish,
        .nextMessageIdCallback = Node::nextMessageId,
    }};

    ControlPlane _controlPlane{_subscriptionManager};

    Publisher _publisher{_transporters, _subscribers};

    Drainer _drainer{{
        .transporters = _transporters,
        .publisher = _publisher,
        .controlPlane = _controlPlane,
        .publishQueue = &_publishQueue,
    }};

    IngressBuffer _ingress;

    using DefaultError = CoreError;
};

inline constexpr LifecycleContract<Node> _node_lifecycle_contract;
inline constexpr TaskControllerContract<Node> _node_task_controller_contract;
inline constexpr TaskController::TaskHooks::Contract<Node>
    _node_task_hooks_contract;

} // namespace Totem::PubSubBackend::detail
