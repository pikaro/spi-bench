#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "PubSubBackend/Interfaces/Config.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Subscriber.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/detail/Concepts.hpp"
#include "PubSubBackend/detail/ControlPlane.hpp"
#include "PubSubBackend/detail/Drainer.hpp"
#include "PubSubBackend/detail/IngressBuffer.hpp"
#include "PubSubBackend/detail/Metrics.hpp"
#include "PubSubBackend/detail/Publisher.hpp"
#include "PubSubBackend/detail/SerDe.hpp"
#include "PubSubBackend/detail/SubscriberDirectory.hpp"
#include "PubSubBackend/detail/SubscriptionManager.hpp"
#include "PubSubBackend/detail/TransportDirectory.hpp"
#include "PubSubBackend/detail/ITransport.hpp"
#include "PubSubBackend/detail/Trace.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Queue/Facade.hpp"
#include "Services/PubSub.hpp"
#include "TaskController/Facade.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "TaskController/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include "Types/Signal.hpp"
#include "magic_enum/magic_enum.hpp"
#include <atomic>
#include <climits>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>

namespace Totem::PubSubBackend::detail {

class Node : public HasLifecycle<Node, Config>,
             public HasTaskController<Node, Config>,
             public INode,
             public ITransportAvailabilityObserver {
    friend class HasLifecycle<Node, Config>;
    friend struct LifecycleContract<Node, Config>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Node, Config>;
    friend struct TaskController::TaskHooks::Contract<Node>;
    friend struct TaskControllerContract<Node>;

    using Transport = typename Spec::Transport;
    using NodeId = typename Spec::NodeId;

  public:
    explicit Node(TaskController::IRegistry &registry,
                  NodeId nodeId = static_cast<NodeId>(Spec::nodeId))
        : HasTaskController<Node, Config>(registry), _nodeId(nodeId) {
        _transporters.enableRegistration();
        _subscribers.enableRegistration();
    }

    DELETE_COPY(Node)
    DELETE_MOVE(Node)

    using Topic = typename Spec::Topic;

    static constexpr const char *name = "PubSub::Node";

    std::expected<TransportId, ReturnCode>
    registerTransport(ITransport &transport) {
        _log_i("%s: registering transport " SV_FMT " (%u)", name,
               SV_ARG(transport.instanceName()), transport.transportId());
        return _transporters.add(transport.transportId(),
                                 transport.instanceName(), transport);
    }

    ReturnCode deregisterTransport(TransportId transportKey) {
        return _transporters.remove(transportKey);
    }

    std::expected<SubscriberKey, ReturnCode>
    subscribe(const char *subscriberName, const Subscriber &subscriber,
              Topic topic) override {
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

    ReturnCode unsubscribe(const SubscriberKey &subscriberKey) override {
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

    ReturnCode publish(const Envelope &envelope) override {
        log_trace_packet("node.publish.enqueue", envelope.header, name);
        _log_d("%s: enqueue publish for " MAGIC_PUBSUB_SV_FMT, name,
               MAGIC_PUBSUB_SV_ARG(envelope.header));
        FAIL_IF_ERR_FWD(Totem::Queue::Platform::send(_publishQueue, &envelope),
                        "Failed to enqueue publish envelope for topic " SV_FMT,
                        MAGIC_SV_ARG(Topic, envelope.header.topic));
        FAIL_IF_ERR_FWD(wake(),
                        "Failed to wake PubSub task after local publish");
        return OK();
    }

    static ReturnCode publish(void *node, const Envelope &envelope) {
        auto *self = static_cast<Node *>(node);
        return self->publish(envelope);
    }

    static ReturnCode ack(void *node, TransportId transportId,
                          const Envelope &envelope) {
        auto *self = static_cast<Node *>(node);
        return self->ack(static_cast<Spec::Transport>(transportId), envelope);
    }

    static ReturnCode ack(void *node, Spec::Transport transport,
                          const Envelope &envelope) {
        auto *self = static_cast<Node *>(node);
        return self->ack(transport, envelope);
    }

    static std::expected<bool, ReturnCode>
    dispatchIngressFrame(void *node, std::span<const std::byte> frame,
                         std::optional<IngressContext> ingress =
                             std::nullopt) {
        auto *self = static_cast<Node *>(node);
        return self->_dispatchIngressFrame(frame, ingress);
    }

    ReturnCode ack(Spec::Transport transport,
                   const Envelope &envelope) override {
        return _drainer.ack(transport, envelope);
    }

    ReturnCode onTransportAvailabilityChanged(TransportId transportId,
                                              bool available) override {
        return _transportAvailabilityChanged(transportId, available);
    }

    ReturnCode wake(Signal signal = Signal::Ping) {
        FAIL_IF(!_taskKey.has_value(), ERR(InvalidState),
                "Cannot wake PubSub task before task registration");
        return _taskController.signalTask(*_taskKey, signal);
    }

    static ReturnCode wake(void *node, Signal signal = Signal::Ping) {
        auto *self = static_cast<Node *>(node);
        return self->wake(signal);
    }

    [[nodiscard]] NodeId nodeId() const { return _nodeId; }
    [[nodiscard]] static Totem::PubSubBackend::NodeId nodeIdHook(void *node) {
        auto *self = static_cast<Node *>(node);
        return static_cast<Totem::PubSubBackend::NodeId>(self->nodeId());
    }
    [[nodiscard]] MessageId nextMessageId() override {
        return _nextMessageId.fetch_add(1, std::memory_order_relaxed);
    }
    [[nodiscard]] static MessageId nextMessageId(void *node) {
        auto *self = static_cast<Node *>(node);
        return self->nextMessageId();
    }
    [[nodiscard]] IngressBuffer &ingress() { return _ingress; }

  private:
    ReturnCode _onBegin() {
        (void)metrics();

        auto taskHooks = TaskController::TaskHooks::bind(*this);

        FAIL_IF_ERR_FWD(_beginTaskController(config().task),
                        "Failed to begin task controller for %s", name);

        auto taskAddResult =
            _taskController.addTask(config().task.name, taskHooks);
        FAIL_IF_UNEXPECTED(task, taskAddResult, taskAddResult.error(),
                           "Failed to bind task hooks for %s", name);
        _taskKey = task;
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
        _taskKey.reset();
        if (_publishQueue != nullptr) {
            ret.combine(Totem::Queue::Platform::destroy(_publishQueue));
            _publishQueue = nullptr;
        }
        return ret;
    }

    static ReturnCode _onTaskNotify(Signal /*signal*/) { return OK(); }

    ReturnCode _transportAvailabilityChanged(TransportId transportId,
                                             bool available) {
        _log_i("%s: transport %u availability changed to %s", name,
               static_cast<unsigned>(transportId),
               available ? "ready" : "not-ready");
        if (!available) {
            return OK();
        }
        _subscriptionReplayPending = true;
        _subscriptionReplayDueMs =
            ::platform::get_time() + subscriptionReplayDelayMs;
        return wake();
    }

    ReturnCode _replaySubscriptionsIfDue() {
        if (!_subscriptionReplayPending) {
            return OK();
        }
        const auto nowMs = ::platform::get_time();
        if (nowMs < _subscriptionReplayDueMs) {
            return OK();
        }
        _subscriptionReplayPending = false;
        return _subscriptionManager.replaySubscriptions();
    }

    std::expected<bool, ReturnCode>
    _dispatchIngressFrame(std::span<const std::byte> frame,
                          std::optional<IngressContext> ingress) {
        FAIL_IF(!ingress.has_value(), std::unexpected(ERR(InvalidArgument)),
                "Transport ingress dispatch requires ingress context");
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            header, SerDe::peekHeader(frame),
            "Failed to peek transport ingress frame header");
        if (ControlPlane::isControlPlaneTopic(header.topic) ||
            _subscriptionManager.subscribed(header.topic)) {
            return false;
        }

        struct DirectDispatch {
            ITransport *transporter = nullptr;
            TransportDispatch dispatch{};
            std::string_view name{};
        };

        size_t dispatchCount = 0;
        DirectDispatch directDispatch;
        FAIL_IF_ERR_FWD_UNEXPECTED(
            _transporters.withAll(
                [&](const TransportId &, const TransporterEntry &entry)
                    -> ReturnCode {
                    auto dispatch =
                        Publisher::dispatchFor(entry, header, ingress);
                    if (!dispatch.has_value()) {
                        return OK();
                    }
                    ++dispatchCount;
                    if (dispatchCount == 1) {
                        directDispatch = DirectDispatch{
                            .transporter = entry.transporter,
                            .dispatch = *dispatch,
                            .name = entry.name,
                        };
                    }
                    return OK();
                }),
            "Failed to scan transports for direct ingress dispatch");

        if (dispatchCount == 0) {
            _log_d("%s: dropping uninterested transport ingress frame "
                   MAGIC_PUBSUB_SV_FMT,
                   name, MAGIC_PUBSUB_SV_ARG(header));
            return true;
        }
        if (dispatchCount > 1) {
            return false;
        }

        auto enqueueRet =
            directDispatch.transporter->enqueueRaw(header, frame,
                                                   directDispatch.dispatch);
        if (enqueueRet.ok()) {
            _log_d("%s: direct-relayed transport ingress frame "
                   MAGIC_PUBSUB_SV_FMT " to " SV_FMT,
                   name, MAGIC_PUBSUB_SV_ARG(header),
                   SV_ARG(directDispatch.name));
            return true;
        }

        if (enqueueRet == ERR(Timeout) || enqueueRet == ERR(Overflow) ||
            enqueueRet == ERR(InvalidState)) {
            _log_d("%s: direct relay backpressured for " MAGIC_PUBSUB_SV_FMT
                   ", falling back to ingress buffering",
                   name, MAGIC_PUBSUB_SV_ARG(header));
            return false;
        }

        FAIL(std::unexpected(enqueueRet),
             "Failed to direct-relay transport ingress frame " ERR_FMT,
             ERR_ARG(enqueueRet));
    }

    ReturnCode _onTaskStep() {
        FAIL_IF_ERR_FWD(_replaySubscriptionsIfDue(),
                        "Failed to replay PubSub subscriptions");
        FAIL_IF_ERR_FWD(_drainer.drain(), "Failed to drain PubSub messages");
        FAIL_IF_UNEXPECTED_FWD(transporters, _transporters.snapshot(),
                               "Failed to snapshot PubSub transporters");
        FAIL_IF_ERR_FWD(
            [&transporters]() {
                auto ret = OK();
                for (size_t i = 0; i < transporters.count; ++i) {
                    ret.combine(transporters.entries[i].transporter->send());
                }
                return ret;
            }(),
            "Failed to work PubSub transporters");
        return OK();
    }

    std::atomic<MessageId> _nextMessageId{1};
    NodeId _nodeId;
    std::optional<TaskController::RunnerKey> _taskKey;

    TransportDirectory _transporters{name};
    SubscriberDirectory _subscribers{name};

    Totem::Queue::Platform::Storage<Envelope, Spec::Limits::maxMessageQueueSize>
        _publishQueueStorage;
    Totem::Queue::Handle _publishQueue = nullptr;

    bool _subscriptionReplayPending = false;
    uint32_t _subscriptionReplayDueMs = 0;
    static constexpr uint32_t subscriptionReplayDelayMs = 50;

    SubscriptionManager _subscriptionManager{SubscriptionManagerDependencies{
        .pubSubNode = this,
        .transporters = _transporters,
        .publishCallback = Node::publish,
        .nextMessageIdCallback = Node::nextMessageId,
        .nodeIdCallback = Node::nodeIdHook,
    }};

    ControlPlane _controlPlane{_subscriptionManager};

    Publisher _publisher{_transporters, _subscribers};

    Drainer _drainer{DrainerDependencies{
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
