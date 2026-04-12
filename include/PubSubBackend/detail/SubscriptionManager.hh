#pragma once

#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Frame.hh"
#include "PubSubBackend/detail/Codec.hh"
#include "PubSubBackend/detail/Pool.hh"
#include "PubSubBackend/detail/TransporterDirectory.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Types/Error.hh"
#include "magic_enum/magic_enum.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>

namespace Totem::PubSubBackend::detail {

enum class SubscribeEventType : uint8_t {
    Register,
    Unregister,
};

struct PubSubEvent {
    TopicId topic;
    SubscribeEventType type;
};

static_assert(std::is_trivially_copyable_v<PubSubEvent>);
static_assert(std::is_standard_layout_v<PubSubEvent>);
static_assert(std::has_unique_object_representations_v<PubSubEvent>);

struct SubscriptionManagerDependencies {
    void *pubSubNode;
    TransporterDirectory &transporters;
    PublishCallback publishCallback;
    NextMessageIdCallback nextMessageIdCallback;

    [[nodiscard]] bool validate() const {
        return pubSubNode != nullptr && publishCallback != nullptr &&
               nextMessageIdCallback != nullptr;
    }
};

class SubscriptionManager {
    using Limits = typename Spec::Limits;
    using Topic = typename Spec::Topic;
    using EventPool = Pool<PubSubEvent, Spec::Limits::maxMessageQueueSize>;

    struct SubscriptionSlot {
        TopicId topic;
        std::atomic<uint8_t> subscriberCount;
    };

  public:
    explicit SubscriptionManager(const SubscriptionManagerDependencies &deps)
        : _pubSubNode(deps.pubSubNode), _publishCallback(deps.publishCallback),
          _eventPool(deps.pubSubNode, deps.nextMessageIdCallback),
          _transporters(deps.transporters) {
        ABORT_IF_NOT(deps.validate(),
                     "Invalid SubscriptionManager dependencies");
    }

    [[nodiscard]] bool subscribed(TopicId topic) const {
        return (_subscribedTopics & topic) != 0;
    }

    ReturnCode registerSubscription(TopicId topic) {
        FAIL_IF(topic == 0, ERR(InvalidArgument),
                "Cannot subscribe to topic 0");

        FAIL_IF_UNEXPECTED_FWD(
            count, _setSubscriberCount(topic, 1),
            "Failed to increment subscriber count for topic " SV_FMT,
            SV_ARG(magic_enum::enum_name(static_cast<Topic>(topic))));

        if (subscribed(topic)) {
            return OK();
        }

        _subscribedTopics |= topic;

        auto event = PubSubEvent{
            .topic = topic,
            .type = SubscribeEventType::Register,
        };

        FAIL_IF_ERR_FWD(
            _sendPubSubEvent(event),
            "Failed to send subscription event for topic " SV_FMT,
            SV_ARG(magic_enum::enum_name(static_cast<Topic>(topic))));
        return OK();
    }

    ReturnCode deregisterSubscription(TopicId topic) {
        FAIL_IF(topic == 0, ERR(InvalidArgument),
                "Cannot subscribe to topic 0");

        if (!subscribed(topic)) {
            FAIL(ERR(NotFound), "Not subscribed to topic " SV_FMT,
                 SV_ARG(magic_enum::enum_name(static_cast<Topic>(topic))));
        }

        FAIL_IF_UNEXPECTED_FWD(
            count, _setSubscriberCount(topic, -1),
            "Failed to decrement subscriber count for topic " SV_FMT,
            SV_ARG(magic_enum::enum_name(static_cast<Topic>(topic))));

        if (count > 0) {
            return OK();
        }

        _subscribedTopics &= ~topic;

        auto event = PubSubEvent{
            .topic = topic,
            .type = SubscribeEventType::Unregister,
        };

        FAIL_IF_ERR_FWD(
            _sendPubSubEvent(event),
            "Failed to send subscription event for topic " SV_FMT,
            SV_ARG(magic_enum::enum_name(static_cast<Topic>(topic))));
        return OK();
    }

    ReturnCode handlePubSubEvent(
        const PublishRequest &request,
        std::optional<TransportId> ingressTransport = std::nullopt) {
        auto topic = static_cast<Topic>(request.topic);
        FAIL_IF(topic != Topic::PubSub, ERR(InvalidArgument),
                "Received non-PubSub event in PubSub event handler");
        FAIL_IF_UNEXPECTED_FWD(
            event, request.getPayload(),
            "Failed to get PubSub event from pool for message ID %u",
            request.messageId);
        return handlePubSubEvent(event, ingressTransport);
    }

    ReturnCode handlePubSubEvent(
        const PubSubEvent &event,
        std::optional<TransportId> ingressTransport = std::nullopt) {
        if (!ingressTransport.has_value()) {
            return OK();
        }

        switch (event.type) {
        case SubscribeEventType::Register:
            return _transporters.subscribeTransport(*ingressTransport,
                                                    event.topic);
        case SubscribeEventType::Unregister:
            return _transporters.unsubscribeTransport(*ingressTransport,
                                                      event.topic);
        default:
            return ERR(InvalidArgument);
        }
        return OK();
    }

  private:
    ReturnCode _sendPubSubEvent(const PubSubEvent &event) {
        FAIL_IF_UNEXPECTED_FWD(messageId, _eventPool.store(event),
                               "Failed to store subscription event");
        FAIL_IF_ERR_FWD(
            _publishCallback(_pubSubNode,
                             PublishRequest{
                                 .messageId = messageId,
                                 .topic = static_cast<TopicId>(Topic::PubSub),
                                 .source = static_cast<NodeId>(Spec::nodeId),
                                 .owner = &_eventPool,
                                 .getPayload = EventPool::getRaw,
                                 .release = EventPool::release,
                             }),
            "Failed to publish event for topic " SV_FMT,
            SV_ARG(magic_enum::enum_name(static_cast<Topic>(event.topic))));
        return OK();
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::expected<uint8_t, ReturnCode> _setSubscriberCount(TopicId topic,
                                                           int8_t delta) {
        FAIL_IF(delta == 0, std::unexpected(ERR(InvalidArgument)),
                "Delta for subscriber count cannot be 0");
        for (auto &slot : _subscriptionSlots) {
            if (slot.topic == topic) {
                FAIL_IF((delta < 0) && (slot.subscriberCount.load(
                                            std::memory_order_relaxed) <
                                        static_cast<uint8_t>(-delta)),
                        std::unexpected(ERR(Underflow)),
                        "Subscriber count underflow");
                FAIL_IF((delta > 0) && (std::numeric_limits<uint8_t>::max() -
                                            slot.subscriberCount.load(
                                                std::memory_order_relaxed) <
                                        static_cast<uint8_t>(delta)),
                        std::unexpected(ERR(Overflow)),
                        "Subscriber count overflow");

                uint8_t count = 0;
                if (delta > 0) {
                    count = slot.subscriberCount.fetch_add(
                        static_cast<uint8_t>(delta), std::memory_order_relaxed);
                } else {
                    count = slot.subscriberCount.fetch_sub(
                        static_cast<uint8_t>(-delta),
                        std::memory_order_relaxed);
                }

                return count + delta;
            }
        }
        if (delta < 0) {
            return std::unexpected(ERR(NotFound));
        }
        for (auto &slot : _subscriptionSlots) {
            uint8_t expected = 0;
            if (slot.subscriberCount.compare_exchange_strong(
                    expected, static_cast<uint8_t>(delta),
                    std::memory_order_relaxed)) {
                slot.topic = topic;
                return delta;
            }
        }
        return std::unexpected(ERR(Overflow));
    }

    void *_pubSubNode;
    TopicMask _subscribedTopics = 0;
    std::array<SubscriptionSlot, Limits::maxTopics> _subscriptionSlots{};
    PublishCallback _publishCallback;
    EventPool _eventPool;
    TransporterDirectory &_transporters;

    using DefaultError = CoreError;
};

} // namespace Totem::PubSubBackend::detail
