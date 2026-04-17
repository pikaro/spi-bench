#pragma once

#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Envelope.hh"
#include "PubSubBackend/Interfaces/Types.hh"
#include "PubSubBackend/detail/Pool.hh"
#include "PubSubBackend/detail/TransporterDirectory.hh"
#include "PubSubBackend/detail/Types.hh"
#include "PubSubBackend/detail/Wire.hh"
#include "Types/Error.hh"
#include "magic_enum/magic_enum.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>

namespace Totem::PubSubBackend::detail {

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
        _log_i("SubscriptionManager: register local subscription for "
               "topic " SV_FMT,
               SV_ARG(magic_enum::enum_name(static_cast<Topic>(topic))));

        FAIL_IF_UNEXPECTED_FWD(
            _, _setSubscriberCount(topic, 1),
            "Failed to increment subscriber count for topic " SV_FMT,
            SV_ARG(magic_enum::enum_name(static_cast<Topic>(topic))));

        if (subscribed(topic)) {
            _log_d("SubscriptionManager: topic " SV_FMT
                   " already subscribed locally",
                   SV_ARG(magic_enum::enum_name(static_cast<Topic>(topic))));
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
        _log_i("SubscriptionManager: deregister local subscription for "
               "topic " SV_FMT,
               SV_ARG(magic_enum::enum_name(static_cast<Topic>(topic))));

        if (!subscribed(topic)) {
            FAIL(ERR(NotFound), "Not subscribed to topic " SV_FMT,
                 SV_ARG(magic_enum::enum_name(static_cast<Topic>(topic))));
        }

        FAIL_IF_UNEXPECTED_FWD(
            count, _setSubscriberCount(topic, -1),
            "Failed to decrement subscriber count for topic " SV_FMT,
            SV_ARG(magic_enum::enum_name(static_cast<Topic>(topic))));

        if (count > 0) {
            _log_d("SubscriptionManager: topic " SV_FMT
                   " still has %u local subscribers",
                   SV_ARG(magic_enum::enum_name(static_cast<Topic>(topic))),
                   count);
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
        const Envelope &envelope,
        std::optional<TransportId> ingressTransport = std::nullopt) {
        auto topic = static_cast<Topic>(envelope.header.topic);
        FAIL_IF(topic != Topic::PubSub, ERR(InvalidArgument),
                "Received non-PubSub event in PubSub event handler");
        FAIL_IF_UNEXPECTED_FWD(
            event, envelope.getPayloadAs<PubSubEvent>(),
            "Failed to get PubSub event from pool for message ID %u",
            envelope.header.messageId);
        _log_d("SubscriptionManager: decoded PubSub event %u for topic " SV_FMT,
               static_cast<unsigned>(event.type),
               SV_ARG(magic_enum::enum_name(static_cast<Topic>(event.topic))));
        return handlePubSubEvent(event, ingressTransport);
    }

    ReturnCode handlePubSubEvent(
        const PubSubEvent &event,
        std::optional<TransportId> ingressTransport = std::nullopt) {
        if (!ingressTransport.has_value()) {
            _log_d(
                "SubscriptionManager: ignoring local PubSub control event "
                "for topic " SV_FMT,
                SV_ARG(magic_enum::enum_name(static_cast<Topic>(event.topic))));
            return OK();
        }

        switch (event.type) {
        case SubscribeEventType::Register:
            _log_i(
                "SubscriptionManager: transport %u subscribed to topic " SV_FMT,
                *ingressTransport,
                SV_ARG(magic_enum::enum_name(static_cast<Topic>(event.topic))));
            return _transporters.subscribeTransport(*ingressTransport,
                                                    event.topic);
        case SubscribeEventType::Unregister:
            _log_i(
                "SubscriptionManager: transport %u unsubscribed from "
                "topic " SV_FMT,
                *ingressTransport,
                SV_ARG(magic_enum::enum_name(static_cast<Topic>(event.topic))));
            return _transporters.unsubscribeTransport(*ingressTransport,
                                                      event.topic);
        default:
            return ERR(InvalidArgument);
        }
        return OK();
    }

  private:
    ReturnCode _sendPubSubEvent(const PubSubEvent &event) {
        _log_d(
            "SubscriptionManager: emitting control event %u for topic " SV_FMT,
            static_cast<unsigned>(event.type),
            SV_ARG(magic_enum::enum_name(static_cast<Topic>(event.topic))));
        FAIL_IF_UNEXPECTED_FWD(messageId, _eventPool.store(event),
                               "Failed to store subscription event");
        auto envelopeResult = Envelope::make<PubSubEvent>({
            .owner = &_eventPool,
            .topic = Topic::PubSub,
            .messageId = messageId,
            .getPayloadPtr = EventPool::getPtr,
            .encodePayload = EventPool::encodePayload,
            .release = EventPool::release,
        });
        FAIL_IF_UNEXPECTED_FWD(
            envelope, envelopeResult,
            "Failed to create envelope for subscription event");
        FAIL_IF_ERR_FWD(
            _publishCallback(_pubSubNode, envelope),
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
};

} // namespace Totem::PubSubBackend::detail
