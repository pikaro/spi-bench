#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Config.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Subscriber.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/detail/Concepts.hpp"
#include "PubSubBackend/detail/ControlPlane.hpp"
#include "PubSubBackend/detail/Drainer.hpp"
#include "PubSubBackend/detail/IngressBuffer.hpp"
#include "PubSubBackend/detail/Publisher.hpp"
#include "PubSubBackend/detail/SubscriberDirectory.hpp"
#include "PubSubBackend/detail/SubscriptionManager.hpp"
#include "PubSubBackend/detail/Transporter.hpp"
#include "PubSubBackend/detail/TransporterDirectory.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Queue/Facade.hpp"
#include "TaskController/Interfaces/RegistryHooks.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "Types/Error.hpp"
#include "Types/Logging.hpp"
#include "magic_enum/magic_enum.hpp"
#include <atomic>
#include <climits>
#include <expected>

namespace Totem::PubSubBackend::detail {

class Node : public HasLifecycle<Node, Config>,
             public HasTaskController<Node, Config> {
    friend class HasLifecycle<Node, Config>;
    friend struct LifecycleContract<Node, Config>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Node, Config>;
    friend struct TaskController::TaskHooks::Contract<Node>;
    friend struct TaskControllerContract<Node>;

    using Transport = typename Spec::Transport;
    using NodeId = typename Spec::NodeId;

  public:
    using TransporterKey = TransporterDirectory::EntryKey;
    using SubscriberKey = SubscriberDirectory::EntryKey;

    explicit Node(TaskController::RegistryHooks registryHooks)
        : HasTaskController<Node, Config>(registryHooks) {
        _transporters.enableRegistration();
        _subscribers.enableRegistration();
    }

    DELETE_COPY(Node)
    DELETE_MOVE(Node)

    using Topic = typename Spec::Topic;

    static constexpr const char *name = "PubSub::Node";

    template <class T>
        requires requires { sizeof(Transporter::Contract<T>); }
    std::expected<TransporterKey, ReturnCode> registerTransport(T &transport) {
        auto transporter = Transporter::bind(transport);
        _log_i("%s: registering transport " SV_FMT " (%u)", name,
               SV_ARG(transport.instanceName()), transport.transportId());
        return _transporters.add(transport.transportId(),
                                 transport.instanceName(), transporter);
    }

    ReturnCode deregisterTransport(TransporterKey transportKey) {
        return _transporters.remove(transportKey);
    }

    std::expected<SubscriberKey, ReturnCode>
    subscribe(const char *subscriberName, const Subscriber &subscriber,
              Topic topic) {
        _log_i("%s: subscribing %s to topic " SV_FMT, name, subscriberName,
               MAGIC_SV_ARG(Topic, topic));
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            subscriberNameKey,
            _subscribers.add(subscriberName, subscriber,
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

    ReturnCode unsubscribe(SubscriberKey subscriberKey) {
        auto topicResult = _subscribers.topicForSubscriber(subscriberKey);
        FAIL_IF_UNEXPECTED_FWD(topic, topicResult,
                               "Failed to get topic for subscriber in %s",
                               _subscribers.ownerName());
        _log_i("%s: unsubscribing subscriber key %u from topic " SV_FMT, name,
               static_cast<unsigned>(subscriberKey),
               MAGIC_SV_ARG(Topic, topic));
        FAIL_IF_ERR_FWD(
            _subscriptionManager.deregisterSubscription(topic),
            "Failed to deregister subscription for topic " SV_FMT,
            SV_ARG(magic_enum::enum_name(static_cast<Topic>(topic))));
        return _subscribers.remove(subscriberKey);
    }

    ReturnCode publish(const Envelope &envelope) {
        _log_d("%s: enqueue publish for " MAGIC_PUBSUB_SV_FMT, name,
               MAGIC_PUBSUB_SV_ARG(envelope.header));
        FAIL_IF_ERR_FWD(Totem::Queue::Platform::send(_publishQueue, &envelope),
                        "Failed to enqueue publish envelope for topic " SV_FMT,
                        MAGIC_SV_ARG(Topic, envelope.header.topic));
        return OK();
    }

    static ReturnCode publish(void *node, const Envelope &envelope) {
        auto *self = static_cast<Node *>(node);
        return self->publish(envelope);
    }

    static ReturnCode ack(void *node, TransportId transportId,
                          const Envelope &envelope) {
        auto *self = static_cast<Node *>(node);
        return self->_drainer.ack(static_cast<Spec::Transport>(transportId),
                                  envelope);
    }

    static ReturnCode ack(void *node, Spec::Transport transport,
                          const Envelope &envelope) {
        auto *self = static_cast<Node *>(node);
        return self->_drainer.ack(transport, envelope);
    }

    [[nodiscard]] static NodeId nodeId() {
        return static_cast<NodeId>(Spec::nodeId);
    }
    [[nodiscard]] MessageId nextMessageId() {
        return _nextMessageId.fetch_add(1, std::memory_order_relaxed);
    }
    [[nodiscard]] static MessageId nextMessageId(void *node) {
        auto *self = static_cast<Node *>(node);
        return self->nextMessageId();
    }
    [[nodiscard]] IngressBuffer &ingress() { return _ingress; }

  private:
    ReturnCode _onBegin() {
        auto taskHooks = TaskController::TaskHooks::bind(*this);

        FAIL_IF_ERR_FWD(_beginTaskController(config().task),
                        "Failed to begin task controller for %s", name);

        auto taskAddResult =
            _taskController.addTask("AggregatorTask", taskHooks);
        FAIL_IF_UNEXPECTED(task, taskAddResult, taskAddResult.error(),
                           "Failed to bind task hooks for %s", name);
        auto publishQueueResult =
            Totem::Queue::Platform::create(_publishQueueStorage);
        FAIL_IF_UNEXPECTED_FWD(publishQueue, publishQueueResult,
                               "Failed to create publish queue: " ERR_FMT,
                               ERR_ARG(publishQueueResult.error()));
        _publishQueue = publishQueue;

        FAIL_IF_ERR_FWD(_taskController.startTask(task, config().task),
                        "Failed to start task for %s", name);
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        _transporters.disableRegistration();
        _subscribers.disableRegistration();
        if (_publishQueue != nullptr) {
            ret.combine(Totem::Queue::Platform::destroy(_publishQueue));
            _publishQueue = nullptr;
        }
        return ret;
    }

    ReturnCode _onTaskStep() {
        FAIL_IF_UNEXPECTED_FWD(transporters, _transporters.snapshot(),
                               "Failed to snapshot PubSub transporters");
        FAIL_IF_ERR_FWD(
            [&transporters]() {
                auto ret = OK();
                for (size_t i = 0; i < transporters.count; ++i) {
                    ret.combine(transporters.entries[i].transporter.receive());
                }
                return ret;
            }(),
            "Failed to receive PubSub transporter input");
        FAIL_IF_ERR_FWD(_drainer.drain(), "Failed to drain PubSub messages");
        FAIL_IF_ERR_FWD(
            [&transporters]() {
                auto ret = OK();
                for (size_t i = 0; i < transporters.count; ++i) {
                    ret.combine(transporters.entries[i].transporter.send());
                }
                return ret;
            }(),
            "Failed to work PubSub transporters");
        return OK();
    }

    std::atomic<MessageId> _nextMessageId{1};

    TransporterDirectory _transporters{name};
    SubscriberDirectory _subscribers{name};

    Totem::Queue::Platform::Storage<Envelope, Spec::Limits::maxMessageQueueSize>
        _publishQueueStorage;
    Totem::Queue::Handle _publishQueue = nullptr;

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

    static const LogComponent logComponent =
        Totem::PubSubBackend::detail::logComponent;
};

inline constexpr LifecycleContract<Node, Config> _node_lifecycle_contract;
inline constexpr TaskControllerContract<Node> _node_task_controller_contract;
inline constexpr TaskController::TaskHooks::Contract<Node>
    _node_task_hooks_contract;
inline constexpr Contract _spec_contract;

} // namespace Totem::PubSubBackend::detail
