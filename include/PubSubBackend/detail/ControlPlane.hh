#pragma once

#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Envelope.hh"
#include "PubSubBackend/Interfaces/Types.hh"
#include "PubSubBackend/detail/SubscriptionManager.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Types/Error.hh"
#include <optional>

namespace Totem::PubSubBackend::detail {

class ControlPlane {
    using Topic = typename Spec::Topic;

  public:
    explicit ControlPlane(SubscriptionManager &subscriptionManager)
        : _subscriptionManager(subscriptionManager) {}

    ReturnCode
    handle(const Envelope &envelope,
           std::optional<TransportId> ingressTransport = std::nullopt) {
        if (!isControlPlaneTopic(envelope.header.topic)) {
            return OK();
        }

        auto topic = static_cast<Topic>(envelope.header.topic);
        if (topic == Topic::PubSub) {
            return _subscriptionManager.handlePubSubEvent(envelope,
                                                          ingressTransport);
        }

        FAIL(ERR(InvalidArgument),
             "Received message with invalid control plane topic " SV_FMT,
             MAGIC_SV_ARG(topic));
    }

    [[nodiscard]] static constexpr bool isControlPlaneTopic(TopicId topicId) {
        auto topic = static_cast<Topic>(topicId);
        return (topic == Topic::PubSub);
    }

  private:
    SubscriptionManager &_subscriptionManager;

    using DefaultError = CoreError;
};

} // namespace Totem::PubSubBackend::detail
