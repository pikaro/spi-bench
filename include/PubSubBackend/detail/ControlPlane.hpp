#pragma once

#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/detail/SubscriptionManager.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Types/Error.hpp"
#include <optional>

namespace Totem::PubSubBackend::detail {

class ControlPlane {
    using Topic = typename Spec::Topic;
    using NodeId = typename Spec::NodeId;

  public:
    explicit ControlPlane(SubscriptionManager &subscriptionManager)
        : _subscriptionManager(subscriptionManager) {}

    ReturnCode
    handle(const Envelope &envelope,
           std::optional<IngressContext> ingressContext = std::nullopt) {
        if (!isControlPlaneTopic(envelope.header.topic)) {
            return OK();
        }

        auto topic = static_cast<Topic>(envelope.header.topic);
        _log_d("ControlPlane: handling topic " SV_FMT
               " for " MAGIC_PUBSUB_SV_FMT,
               SV_ARG(magic_enum::enum_name(topic)),
               MAGIC_PUBSUB_SV_ARG(envelope.header));
        if (topic == Topic::PubSub) {
            return _subscriptionManager.handlePubSubEvent(envelope,
                                                          ingressContext);
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
};

} // namespace Totem::PubSubBackend::detail
